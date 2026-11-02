#include "npu_yolo_postprocess.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace boardlab::npu {
namespace {

float clampFloat(float value, float low, float high)
{
    return std::max(low, std::min(value, high));
}

float iou(const cv::Rect &left, const cv::Rect &right)
{
    const int intersection = (left & right).area();
    const int combined = left.area() + right.area() - intersection;
    return combined > 0 ? static_cast<float>(intersection) / static_cast<float>(combined) : 0.0f;
}

std::vector<int> nmsIndices(const std::vector<Detection> &detections, float threshold)
{
    std::vector<int> order(detections.size());
    for (size_t index = 0; index < detections.size(); ++index) {
        order[index] = static_cast<int>(index);
    }
    std::sort(order.begin(), order.end(), [&detections](int left, int right) {
        return detections[left].confidence > detections[right].confidence;
    });

    std::vector<bool> suppressed(detections.size(), false);
    std::vector<int> kept;
    kept.reserve(order.size());
    for (size_t index = 0; index < order.size(); ++index) {
        const int current = order[index];
        if (suppressed[current]) {
            continue;
        }
        kept.push_back(current);
        for (size_t next = index + 1; next < order.size(); ++next) {
            const int candidate = order[next];
            if (suppressed[candidate] || detections[current].classId != detections[candidate].classId) {
                continue;
            }
            if (iou(detections[current].box, detections[candidate].box) > threshold) {
                suppressed[candidate] = true;
            }
        }
    }
    return kept;
}

float tensorValue(const Tensor &tensor, int index)
{
    if (!tensor.data || index < 0) {
        return 0.0f;
    }
    switch (tensor.type) {
    case TensorType::Int8:
        return (static_cast<float>(static_cast<const int8_t *>(tensor.data)[index])
                - static_cast<float>(tensor.zeroPoint)) * tensor.scale;
    case TensorType::UInt8:
        return (static_cast<float>(static_cast<const uint8_t *>(tensor.data)[index])
                - static_cast<float>(tensor.zeroPoint)) * tensor.scale;
    case TensorType::Float32:
        return static_cast<const float *>(tensor.data)[index];
    }
    return 0.0f;
}

void decodeDflBox(const Tensor &tensor, int offset, int gridLength, int dflLength, float *box)
{
    for (int side = 0; side < 4; ++side) {
        float maximum = -std::numeric_limits<float>::infinity();
        for (int bucket = 0; bucket < dflLength; ++bucket) {
            maximum = std::max(maximum,
                               tensorValue(tensor, (side * dflLength + bucket) * gridLength + offset));
        }

        float exponentSum = 0.0f;
        float weightedSum = 0.0f;
        for (int bucket = 0; bucket < dflLength; ++bucket) {
            const float exponent = std::exp(
                tensorValue(tensor, (side * dflLength + bucket) * gridLength + offset) - maximum);
            exponentSum += exponent;
            weightedSum += exponent * static_cast<float>(bucket);
        }
        box[side] = exponentSum > 0.0f ? weightedSum / exponentSum : 0.0f;
    }
}

Detection makeDetection(int classId,
                        float confidence,
                        float centerX,
                        float centerY,
                        float width,
                        float height,
                        const LetterboxInfo &letterbox,
                        const std::vector<std::string> &labels)
{
    const float x1 = clampFloat((centerX - width * 0.5f - static_cast<float>(letterbox.padX))
                                    / letterbox.scale,
                                0.0f,
                                static_cast<float>(letterbox.originalWidth - 1));
    const float y1 = clampFloat((centerY - height * 0.5f - static_cast<float>(letterbox.padY))
                                    / letterbox.scale,
                                0.0f,
                                static_cast<float>(letterbox.originalHeight - 1));
    const float x2 = clampFloat((centerX + width * 0.5f - static_cast<float>(letterbox.padX))
                                    / letterbox.scale,
                                0.0f,
                                static_cast<float>(letterbox.originalWidth - 1));
    const float y2 = clampFloat((centerY + height * 0.5f - static_cast<float>(letterbox.padY))
                                    / letterbox.scale,
                                0.0f,
                                static_cast<float>(letterbox.originalHeight - 1));

    Detection detection;
    detection.classId = classId;
    detection.label = classId >= 0 && classId < static_cast<int>(labels.size())
        ? labels[static_cast<size_t>(classId)]
        : std::to_string(classId);
    detection.confidence = confidence;
    detection.box = cv::Rect(static_cast<int>(std::round(x1)),
                             static_cast<int>(std::round(y1)),
                             static_cast<int>(std::round(x2 - x1)),
                             static_cast<int>(std::round(y2 - y1)));
    return detection;
}

} // namespace

std::vector<std::string> loadLabels(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open YOLOv8 labels: " + path);
    }

    std::vector<std::string> labels;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            labels.push_back(line);
        }
    }
    return labels;
}

cv::Mat letterboxBgr(const cv::Mat &image, int inputWidth, int inputHeight, LetterboxInfo *info)
{
    if (image.empty() || inputWidth <= 0 || inputHeight <= 0) {
        throw std::runtime_error("invalid YOLOv8 input image or input size");
    }
    const float scale = std::min(static_cast<float>(inputWidth) / static_cast<float>(image.cols),
                                 static_cast<float>(inputHeight) / static_cast<float>(image.rows));
    const int resizedWidth = static_cast<int>(std::round(image.cols * scale));
    const int resizedHeight = static_cast<int>(std::round(image.rows * scale));
    const int padX = (inputWidth - resizedWidth) / 2;
    const int padY = (inputHeight - resizedHeight) / 2;

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(resizedWidth, resizedHeight));
    cv::Mat output(inputHeight, inputWidth, CV_8UC3, cv::Scalar(128, 128, 128));
    resized.copyTo(output(cv::Rect(padX, padY, resizedWidth, resizedHeight)));
    if (info) {
        info->scale = scale;
        info->padX = padX;
        info->padY = padY;
        info->inputWidth = inputWidth;
        info->inputHeight = inputHeight;
        info->originalWidth = image.cols;
        info->originalHeight = image.rows;
    }
    return output;
}

std::vector<Detection> decodeYoloV8Branches(const std::vector<YoloV8BranchOutput> &branches,
                                             int classCount,
                                             float confidenceThreshold,
                                             float nmsThreshold,
                                             const LetterboxInfo &letterbox,
                                             const std::vector<std::string> &labels)
{
    std::vector<Detection> candidates;
    if (classCount <= 0 || letterbox.scale <= 0.0f) {
        return candidates;
    }

    for (const YoloV8BranchOutput &branch : branches) {
        if (!branch.box.data || !branch.score.data || branch.gridHeight <= 0 || branch.gridWidth <= 0
            || branch.stride <= 0 || branch.dflLength <= 0) {
            continue;
        }
        const int gridLength = branch.gridHeight * branch.gridWidth;
        for (int y = 0; y < branch.gridHeight; ++y) {
            for (int x = 0; x < branch.gridWidth; ++x) {
                const int offset = y * branch.gridWidth + x;
                if (branch.hasScoreSum && tensorValue(branch.scoreSum, offset) < confidenceThreshold) {
                    continue;
                }
                int bestClass = -1;
                float bestScore = 0.0f;
                for (int classId = 0; classId < classCount; ++classId) {
                    const float score = tensorValue(branch.score, classId * gridLength + offset);
                    if (score > bestScore) {
                        bestScore = score;
                        bestClass = classId;
                    }
                }
                if (bestClass < 0 || bestScore < confidenceThreshold) {
                    continue;
                }

                float dfl[4] {};
                decodeDflBox(branch.box, offset, gridLength, branch.dflLength, dfl);
                const float x1 = (-dfl[0] + static_cast<float>(x) + 0.5f) * branch.stride;
                const float y1 = (-dfl[1] + static_cast<float>(y) + 0.5f) * branch.stride;
                const float x2 = (dfl[2] + static_cast<float>(x) + 0.5f) * branch.stride;
                const float y2 = (dfl[3] + static_cast<float>(y) + 0.5f) * branch.stride;
                const float width = x2 - x1;
                const float height = y2 - y1;
                if (width <= 1.0f || height <= 1.0f) {
                    continue;
                }
                Detection detection = makeDetection(bestClass,
                                                    bestScore,
                                                    x1 + width * 0.5f,
                                                    y1 + height * 0.5f,
                                                    width,
                                                    height,
                                                    letterbox,
                                                    labels);
                if (detection.box.width > 1 && detection.box.height > 1) {
                    candidates.push_back(std::move(detection));
                }
            }
        }
    }

    const std::vector<int> kept = nmsIndices(candidates, nmsThreshold);
    std::vector<Detection> detections;
    detections.reserve(kept.size());
    for (const int index : kept) {
        detections.push_back(candidates[static_cast<size_t>(index)]);
    }
    return detections;
}

void drawDetections(cv::Mat *image, const std::vector<Detection> &detections)
{
    if (!image || image->empty()) {
        return;
    }
    for (const Detection &detection : detections) {
        const cv::Scalar color(0, 220, 0);
        cv::rectangle(*image, detection.box, color, 2, cv::LINE_AA);
        std::ostringstream text;
        text << detection.label << ' ' << std::fixed << std::setprecision(2) << detection.confidence;
        int baseline = 0;
        const cv::Size textSize = cv::getTextSize(text.str(), cv::FONT_HERSHEY_SIMPLEX, 0.52, 1, &baseline);
        const int labelX = std::max(0, detection.box.x);
        const int labelY = std::max(textSize.height + 6, detection.box.y);
        cv::rectangle(*image,
                      cv::Rect(labelX, labelY - textSize.height - 6, textSize.width + 8, textSize.height + 7),
                      color,
                      cv::FILLED);
        cv::putText(*image,
                    text.str(),
                    cv::Point(labelX + 4, labelY - 4),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.52,
                    cv::Scalar(0, 0, 0),
                    1,
                    cv::LINE_AA);
    }
}

} // namespace boardlab::npu

