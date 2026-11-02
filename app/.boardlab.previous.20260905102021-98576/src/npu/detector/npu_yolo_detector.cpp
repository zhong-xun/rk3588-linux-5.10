#include "npu_yolo_detector.h"

#include <opencv2/imgproc.hpp>

#include <rknn_api.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace boardlab::npu {
namespace {

std::vector<uint8_t> readBinaryFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open YOLOv8 RKNN model: " + path);
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("YOLOv8 RKNN model is empty: " + path);
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size)) {
        throw std::runtime_error("failed to read YOLOv8 RKNN model: " + path);
    }
    return bytes;
}

void checkRknn(int result, const char *operation)
{
    if (result < RKNN_SUCC) {
        throw std::runtime_error(std::string(operation) + " failed (RKNN=" + std::to_string(result) + ')');
    }
}

TensorType tensorType(const rknn_tensor_attr &attribute)
{
    switch (attribute.type) {
    case RKNN_TENSOR_INT8:
        return TensorType::Int8;
    case RKNN_TENSOR_UINT8:
        return TensorType::UInt8;
    default:
        return TensorType::Float32;
    }
}

bool quantized(const rknn_tensor_attr &attribute)
{
    return attribute.type == RKNN_TENSOR_INT8 || attribute.type == RKNN_TENSOR_UINT8;
}

int dimension(const rknn_tensor_attr &attribute, int index)
{
    if (index < 0 || index >= static_cast<int>(attribute.n_dims)) {
        return 0;
    }
    return static_cast<int>(attribute.dims[index]);
}

rknn_core_mask coreMask(int coreId)
{
    switch (coreId) {
    case 0:
        return RKNN_NPU_CORE_0;
    case 1:
        return RKNN_NPU_CORE_1;
    case 2:
        return RKNN_NPU_CORE_2;
    default:
        return RKNN_NPU_CORE_AUTO;
    }
}

} // namespace

struct YoloV8RknnDetector::Impl {
    rknn_context context = 0;
    rknn_input_output_num ioCount {};
    std::vector<rknn_tensor_attr> inputAttributes;
    std::vector<rknn_tensor_attr> outputAttributes;
};

YoloV8RknnDetector::YoloV8RknnDetector(YoloV8Options options)
    : m_options(std::move(options))
    , m_impl(std::make_unique<Impl>())
{
}

YoloV8RknnDetector::~YoloV8RknnDetector()
{
    if (m_impl && m_impl->context != 0) {
        rknn_destroy(m_impl->context);
        m_impl->context = 0;
    }
}

void YoloV8RknnDetector::initialize()
{
    m_labels = loadLabels(m_options.labelPath);
    if (m_labels.size() < static_cast<size_t>(m_options.classCount)) {
        throw std::runtime_error("YOLOv8 label file has fewer labels than the configured class count");
    }

    const std::vector<uint8_t> model = readBinaryFile(m_options.modelPath);
    checkRknn(rknn_init(&m_impl->context,
                        const_cast<uint8_t *>(model.data()),
                        static_cast<uint32_t>(model.size()),
                        0,
                        nullptr),
              "rknn_init");

    try {
        if (m_options.npuCoreId >= 0) {
            checkRknn(rknn_set_core_mask(m_impl->context, coreMask(m_options.npuCoreId)),
                      "rknn_set_core_mask");
        }
        checkRknn(rknn_query(m_impl->context,
                             RKNN_QUERY_IN_OUT_NUM,
                             &m_impl->ioCount,
                             sizeof(m_impl->ioCount)),
                  "rknn_query inputs/outputs");
        if (m_impl->ioCount.n_input != 1
            || (m_impl->ioCount.n_output != 6 && m_impl->ioCount.n_output != 9)) {
            throw std::runtime_error("YOLOv8 RKNN model must expose one input and six or nine outputs");
        }

        m_impl->inputAttributes.resize(m_impl->ioCount.n_input);
        for (uint32_t index = 0; index < m_impl->ioCount.n_input; ++index) {
            m_impl->inputAttributes[index].index = index;
            checkRknn(rknn_query(m_impl->context,
                                 RKNN_QUERY_INPUT_ATTR,
                                 &m_impl->inputAttributes[index],
                                 sizeof(rknn_tensor_attr)),
                      "rknn_query input tensor");
        }

        m_impl->outputAttributes.resize(m_impl->ioCount.n_output);
        for (uint32_t index = 0; index < m_impl->ioCount.n_output; ++index) {
            m_impl->outputAttributes[index].index = index;
            checkRknn(rknn_query(m_impl->context,
                                 RKNN_QUERY_OUTPUT_ATTR,
                                 &m_impl->outputAttributes[index],
                                 sizeof(rknn_tensor_attr)),
                      "rknn_query output tensor");
        }
    } catch (...) {
        rknn_destroy(m_impl->context);
        m_impl->context = 0;
        throw;
    }
}

std::vector<Detection> YoloV8RknnDetector::infer(const cv::Mat &bgrFrame)
{
    if (bgrFrame.empty()) {
        return {};
    }
    if (!m_impl || m_impl->context == 0) {
        throw std::runtime_error("YOLOv8 detector was not initialized");
    }

    LetterboxInfo letterbox;
    cv::Mat inputBgr = letterboxBgr(bgrFrame, m_options.inputWidth, m_options.inputHeight, &letterbox);
    cv::Mat inputImage;
    if (m_options.rgbInput) {
        cv::cvtColor(inputBgr, inputImage, cv::COLOR_BGR2RGB);
    } else {
        inputImage = inputBgr;
    }
    if (!inputImage.isContinuous()) {
        inputImage = inputImage.clone();
    }

    rknn_input input {};
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.size = static_cast<uint32_t>(inputImage.total() * inputImage.elemSize());
    input.fmt = RKNN_TENSOR_NHWC;
    input.buf = inputImage.data;
    input.pass_through = 0;
    checkRknn(rknn_inputs_set(m_impl->context, 1, &input), "rknn_inputs_set");
    checkRknn(rknn_run(m_impl->context, nullptr), "rknn_run");

    std::vector<rknn_output> outputs(m_impl->ioCount.n_output);
    for (uint32_t index = 0; index < m_impl->ioCount.n_output; ++index) {
        outputs[index].index = index;
        outputs[index].want_float = quantized(m_impl->outputAttributes[index]) ? 0 : 1;
        outputs[index].is_prealloc = 0;
    }
    checkRknn(rknn_outputs_get(m_impl->context,
                               m_impl->ioCount.n_output,
                               outputs.data(),
                               nullptr),
              "rknn_outputs_get");

    try {
        const int outputsPerBranch = static_cast<int>(m_impl->ioCount.n_output / 3U);
        std::vector<YoloV8BranchOutput> branches;
        branches.reserve(3);
        for (int branchIndex = 0; branchIndex < 3; ++branchIndex) {
            const int boxIndex = branchIndex * outputsPerBranch;
            const int scoreIndex = boxIndex + 1;
            const rknn_tensor_attr &boxAttribute = m_impl->outputAttributes[static_cast<size_t>(boxIndex)];
            const rknn_tensor_attr &scoreAttribute = m_impl->outputAttributes[static_cast<size_t>(scoreIndex)];
            const int gridHeight = dimension(boxAttribute, static_cast<int>(boxAttribute.n_dims) - 2);
            const int gridWidth = dimension(boxAttribute, static_cast<int>(boxAttribute.n_dims) - 1);
            const int dflChannels = dimension(boxAttribute, static_cast<int>(boxAttribute.n_dims) - 3);
            if (gridHeight <= 0 || gridWidth <= 0 || dflChannels <= 0 || dflChannels % 4 != 0) {
                throw std::runtime_error("unsupported YOLOv8 RKNN output tensor shape");
            }

            YoloV8BranchOutput branch;
            branch.box = {outputs[static_cast<size_t>(boxIndex)].buf,
                          tensorType(boxAttribute),
                          boxAttribute.zp,
                          boxAttribute.scale};
            branch.score = {outputs[static_cast<size_t>(scoreIndex)].buf,
                            tensorType(scoreAttribute),
                            scoreAttribute.zp,
                            scoreAttribute.scale};
            if (outputsPerBranch == 3) {
                const int scoreSumIndex = boxIndex + 2;
                const rknn_tensor_attr &sumAttribute = m_impl->outputAttributes[static_cast<size_t>(scoreSumIndex)];
                branch.scoreSum = {outputs[static_cast<size_t>(scoreSumIndex)].buf,
                                   tensorType(sumAttribute),
                                   sumAttribute.zp,
                                   sumAttribute.scale};
                branch.hasScoreSum = true;
            }
            branch.gridHeight = gridHeight;
            branch.gridWidth = gridWidth;
            branch.stride = m_options.inputHeight / gridHeight;
            branch.dflLength = dflChannels / 4;
            branches.push_back(branch);
        }

        std::vector<Detection> detections = decodeYoloV8Branches(branches,
                                                                   m_options.classCount,
                                                                   m_options.confidenceThreshold,
                                                                   m_options.nmsThreshold,
                                                                   letterbox,
                                                                   m_labels);
        rknn_outputs_release(m_impl->context, m_impl->ioCount.n_output, outputs.data());
        return detections;
    } catch (...) {
        rknn_outputs_release(m_impl->context, m_impl->ioCount.n_output, outputs.data());
        throw;
    }
}

} // namespace boardlab::npu
