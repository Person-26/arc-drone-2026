#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
	cv::VideoCapture cap;
	cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
	cap.open("rtsp://tocatta:8554/cam?tcp", cv::CAP_FFMPEG);

	if (!cap.isOpened()) {
		std::cerr << "Failed to open stream" << std::endl;
		return -1;
	}

	cv::Mat frame;
	while (cap.read(frame) && !frame.empty()) {
		cv::imshow("frame", frame);
		if ((cv::waitKey(1) & 0xFF) == 'q')
			break;
	}

	cap.release();
	cv::destroyAllWindows();
	return 0;
}

// Local variables:
// c-basic-offset: 4
// tab-width: 4
// indent-tabs-mode: t
// End:
