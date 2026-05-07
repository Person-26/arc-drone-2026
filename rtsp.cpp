#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

using namespace cv;

int main() {
	static const int targetWidth = 640;
    static const int targetHeight = 480;

	VideoCapture cap;
	cap.set(CAP_PROP_BUFFERSIZE, 1);
	cap.open("rtsp://tocatta:8554/cam?tcp", CAP_FFMPEG);

	if (!cap.isOpened()) {
		std::cerr << "Failed to open stream" << std::endl;
		return -1;
	}

	Mat frame;
	while (cap.read(frame) && !frame.empty()) {
        // Convert to HSV for color-based detection
        Mat hsv;
        cvtColor(frame, hsv, COLOR_BGR2HSV);

        // Threshold red color (red wraps around HSV hue range)
        Mat redMask1, redMask2, redMask;
        inRange(hsv, Scalar(0, 120, 70), Scalar(10, 255, 255), redMask1);
        inRange(hsv, Scalar(170, 120, 70), Scalar(180, 255, 255), redMask2);
        bitwise_or(redMask1, redMask2, redMask);

        // Clean mask noise and smooth edges
        Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(5, 5));
        morphologyEx(redMask, redMask, MORPH_OPEN, kernel);
        morphologyEx(redMask, redMask, MORPH_CLOSE, kernel);

        Mat blurred;
        GaussianBlur(redMask, blurred, Size(9, 9), 2, 2);

        // Detect circles on red regions
		std::vector<Vec3f> circles;
        HoughCircles(blurred, circles, HOUGH_GRADIENT, 1, 50,
                    120, 20, 10, 300);

        // Draw detected circles
        for (size_t i = 0; i < circles.size(); i++) {
            Vec3i c = circles[i];
            circle(frame, Point(c[0], c[1]), c[2], Scalar(0, 255, 0), 2);
            circle(frame, Point(c[0], c[1]), 2, Scalar(255, 255, 255), 3);
        }

        // Encode frame as JPEG
        // Resize to target resolution to reduce encoding size/latency
        Mat outFrame;
        resize(frame, outFrame, Size(targetWidth, targetHeight));

		imshow("frame", outFrame);
		if ((waitKey(1) & 0xFF) == 'q')
			break;
	}

	cap.release();
	destroyAllWindows();
	return 0;
}

// Local variables:
// c-basic-offset: 4
// tab-width: 4
// indent-tabs-mode: t
// End:
