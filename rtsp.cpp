#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

using namespace cv;

int main(int argc, char *argv[]) {
	int threshold = 10000;
	int saturation = 150;

	for (int i = 1; i < argc; i++) {
		if (std::string(argv[i]) == "--threshold" && i + 1 < argc)
			threshold = std::stoi(argv[++i]);
		else if (std::string(argv[i]) == "--saturation" && i + 1 < argc)
			saturation = std::stoi(argv[++i]);
		else {
			std::cerr << "Usage: " << argv[0] << " [--threshold N] [--saturation N]" << std::endl;
			return 1;
		}
	}
	
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
		inRange(hsv, Scalar(0,   saturation, 100), Scalar(10,  255, 255), redMask1);
		inRange(hsv, Scalar(170, saturation, 100), Scalar(180, 255, 255), redMask2);
        bitwise_or(redMask1, redMask2, redMask);

		// Find contours
        std::vector<std::vector<Point>> contours;
        findContours(redMask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        for (auto& contour : contours) {
            double area = contourArea(contour);
            if (area > threshold) {  // tune this threshold
                Rect bbox = boundingRect(contour);
                rectangle(frame, bbox, Scalar(0, 255, 0), 2);
                std::cout << "Red area detected: " << area << " px\n";
            }
        }

		imshow("frame", frame);
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
