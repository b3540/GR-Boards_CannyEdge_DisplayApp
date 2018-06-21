#include "mbed.h"
#include "EasyAttach_CameraAndLCD.h"
#include "opencv.hpp"
#include "dcache-control.h"
#include "JPEG_Converter.h"
#include "DisplayApp.h"

/**** User Selection *********/
/** JPEG out setting **/
#define JPEG_ENCODE_QUALITY    (75)    /* JPEG encode quality (min:1, max:75 (Considering the size of JpegBuffer, about 75 is the upper limit.)) */
/*****************************/

#define VIDEO_PIXEL_HW         (640u)
#define VIDEO_PIXEL_VW         (360u)

/*! Frame buffer stride: Frame buffer stride should be set to a multiple of 32 or 128
    in accordance with the frame buffer burst transfer mode. */
#define FRAME_BUFFER_STRIDE    (((VIDEO_PIXEL_HW * 2) + 31u) & ~31u)
#define FRAME_BUFFER_HEIGHT    (VIDEO_PIXEL_VW)

static DisplayBase Display;
static Thread mainTask(osPriorityNormal, (1024 * 8));

#if defined(__ICCARM__)
#pragma data_alignment=32
static uint8_t FrameBuffer_Video[FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT]@ ".mirrorram";
#pragma data_alignment=32
static uint8_t FrameBuffer_Result[FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT]@ ".mirrorram";
#pragma data_alignment=32
static uint8_t JpegBuffer[1024 * 100]@ ".mirrorram";
#else
static uint8_t FrameBuffer_Video[FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT]__attribute((section("NC_BSS"),aligned(32)));
static uint8_t FrameBuffer_Result[FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT]__attribute((section("NC_BSS"),aligned(32)));
static uint8_t JpegBuffer[1024 * 100]__attribute((section("NC_BSS"),aligned(32)));
#endif
static JPEG_Converter Jcu;
static DisplayApp  display_app;
static bool vfield_flg = false;

static cv::Mat src_img(VIDEO_PIXEL_VW, VIDEO_PIXEL_HW, CV_8UC2, FrameBuffer_Video);
static cv::Mat result_img(VIDEO_PIXEL_VW, VIDEO_PIXEL_HW, CV_8UC2, FrameBuffer_Result);

static InterruptIn btn0(USER_BUTTON0);
static int btn0_type = 0;

#define BTN0_TYPE_MAX        (1)

static void IntCallbackFunc_Vfield(DisplayBase::int_type_t int_type) {
    vfield_flg = true;
}

static void Start_Video_Camera(void) {
    // Initialize the background to black
    for (uint32_t i = 0; i < sizeof(FrameBuffer_Video); i += 2) {
        FrameBuffer_Video[i + 0] = 0x10;
        FrameBuffer_Video[i + 1] = 0x80;
    }

    Display.Graphics_Irq_Handler_Set(DisplayBase::INT_TYPE_S0_VFIELD, 0, IntCallbackFunc_Vfield);

    // Video capture setting (progressive form fixed)
    Display.Video_Write_Setting(
        DisplayBase::VIDEO_INPUT_CHANNEL_0,
        DisplayBase::COL_SYS_NTSC_358,
        (void *)FrameBuffer_Video,
        FRAME_BUFFER_STRIDE,
        DisplayBase::VIDEO_FORMAT_YCBCR422,
        DisplayBase::WR_RD_WRSWA_32_16BIT,
        VIDEO_PIXEL_VW,
        VIDEO_PIXEL_HW
    );
    EasyAttach_CameraStart(Display, DisplayBase::VIDEO_INPUT_CHANNEL_0);
}

static void convert_gray2yuv422(cv::Mat& src, cv::Mat& dst) {
    uint32_t src_size  = src.cols * src.rows;
    uint32_t dst_size  = dst.cols * dst.rows;
    uint32_t size;
    uint32_t idx = 0;

    if (src_size < dst_size) {
        size = src_size;
    } else {
        size = dst_size;
    }

    for (uint32_t i = 0; i < size; i++) {
        dst.data[idx++] = src.data[i];
        dst.data[idx++] = 0x80;
    }
}

static void btn0_fall(void) {
    if (btn0_type < BTN0_TYPE_MAX) {
        btn0_type++;
    } else {
        btn0_type = 0;
    }
}

static void main_task(void) {
    cv::Mat canny_img;
    JPEG_Converter::bitmap_buff_info_t bitmap_buff_info;
    JPEG_Converter::encode_options_t   encode_options;
    size_t jcu_encode_size;

    btn0.fall(&btn0_fall);
    Jcu.SetQuality(JPEG_ENCODE_QUALITY);
    EasyAttach_Init(Display, VIDEO_PIXEL_HW, VIDEO_PIXEL_VW);
    Start_Video_Camera();

    // JPEG setting
    bitmap_buff_info.width  = VIDEO_PIXEL_HW;
    bitmap_buff_info.height = VIDEO_PIXEL_VW;
    bitmap_buff_info.format = JPEG_Converter::WR_RD_YCbCr422;
    encode_options.encode_buff_size  = sizeof(JpegBuffer);
    encode_options.input_swapsetting = JPEG_Converter::WR_RD_WRSWA_32_16_8BIT;

    while (1) {
        while (vfield_flg == false) {
            Thread::wait(2);
        }
        vfield_flg = false;

        switch (btn0_type) {
            case 0: // camera
                bitmap_buff_info.buffer_address = &src_img.data[0];
                break;
            default:
            case 1: // Canny
                // Grayscale
                cv::cvtColor(src_img, result_img, cv::COLOR_YUV2GRAY_YUY2);
                // Canny
                cv::Canny(result_img, canny_img, 100, 200);
                // YUV
                convert_gray2yuv422(canny_img, result_img);
                dcache_clean(&result_img.data[0], FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT);

                bitmap_buff_info.buffer_address = &result_img.data[0];
                break;
        }

        jcu_encode_size = 0;
        if (Jcu.encode(&bitmap_buff_info, JpegBuffer, &jcu_encode_size, &encode_options) == JPEG_Converter::JPEG_CONV_OK) {
            display_app.SendJpeg(JpegBuffer, (int)jcu_encode_size);
        }
    }
}

int main(void) {
    mainTask.start(callback(main_task));
    mainTask.join();
}
