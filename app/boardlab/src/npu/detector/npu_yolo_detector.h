#pragma once

#include "npu_yolo_postprocess.h"

#include <memory>
#include <string>
#include <vector>

namespace boardlab::npu {

// Fixed model options for BoardLab's target-specific YOLOv8 demo. Callers do
// not pass these from QML or HTTP; NpuYoloDemo fills each value explicitly.
struct YoloV8Options {
    std::string modelPath;
    std::string labelPath;
    int inputWidth = 640;
    int inputHeight = 640;
    int classCount = 80;
    float confidenceThreshold = 0.25f;
    float nmsThreshold = 0.45f;
    bool rgbInput = true;
    bool letterbox = true;
    // -1 keeps RKNN automatic scheduling. Explicit masks are used only by the
    // RK3588 execution policy; other SoCs must not call rknn_set_core_mask.
    int npuCoreId = -1;
};

class YoloV8RknnDetector final {
public:
    explicit YoloV8RknnDetector(YoloV8Options options);
    ~YoloV8RknnDetector();

    YoloV8RknnDetector(const YoloV8RknnDetector &) = delete;
    YoloV8RknnDetector &operator=(const YoloV8RknnDetector &) = delete;

    void initialize();
    std::vector<Detection> infer(const cv::Mat &bgrFrame);

private:
    struct Impl;

    YoloV8Options m_options;
    std::vector<std::string> m_labels;
    std::unique_ptr<Impl> m_impl;
};

} // namespace boardlab::npu
