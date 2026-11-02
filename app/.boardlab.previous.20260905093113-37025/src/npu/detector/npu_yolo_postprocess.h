#pragma once

// This is a focused, namespaced extraction of the YOLOv8 post-processing
// used by multichannel_ai_nvr.  It intentionally only carries the path used
// by BoardLab's fixed YOLOv8 demo and has no public exposure outside the
// optional RKNN/OpenCV build.

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace boardlab::npu {

struct Detection {
    int classId = -1;
    std::string label;
    float confidence = 0.0f;
    cv::Rect box;
};

struct LetterboxInfo {
    float scale = 1.0f;
    int padX = 0;
    int padY = 0;
    int inputWidth = 0;
    int inputHeight = 0;
    int originalWidth = 0;
    int originalHeight = 0;
};

enum class TensorType {
    Float32,
    Int8,
    UInt8,
};

struct Tensor {
    const void *data = nullptr;
    TensorType type = TensorType::Float32;
    int32_t zeroPoint = 0;
    float scale = 1.0f;
};

struct YoloV8BranchOutput {
    Tensor box;
    Tensor score;
    Tensor scoreSum;
    bool hasScoreSum = false;
    int gridHeight = 0;
    int gridWidth = 0;
    int stride = 0;
    int dflLength = 16;
};

std::vector<std::string> loadLabels(const std::string &path);
cv::Mat letterboxBgr(const cv::Mat &image, int inputWidth, int inputHeight, LetterboxInfo *info);
std::vector<Detection> decodeYoloV8Branches(const std::vector<YoloV8BranchOutput> &branches,
                                             int classCount,
                                             float confidenceThreshold,
                                             float nmsThreshold,
                                             const LetterboxInfo &letterbox,
                                             const std::vector<std::string> &labels);
void drawDetections(cv::Mat *image, const std::vector<Detection> &detections);

} // namespace boardlab::npu

