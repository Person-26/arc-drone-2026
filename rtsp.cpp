#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <tbb/concurrent_queue.h>

class FrameQueue {
    tbb::concurrent_bounded_queue<cv::Mat> q;
public:
    explicit FrameQueue(int capacity) {
        q.set_capacity(capacity);
    }
    bool try_push(const cv::Mat& frame) { return q.try_push(frame); }
    bool try_pop(cv::Mat& frame) { return q.try_pop(frame); }
};

static FrameQueue frameQueue(4);
static std::atomic<bool> running(true);

void captureThread(cv::VideoCapture& cap) {
	while (running) {
		cv::Mat frame;
		if (cap.read(frame) && !frame.empty())
			frameQueue.try_push(frame.clone());
		else
			break;
	}
}

int main() {
	cv::VideoCapture cap;
	cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
	cap.open("rtsp://tocatta:8554/cam?tcp", cv::CAP_FFMPEG);
 
	if (!cap.isOpened()) {
		std::cerr << "Failed to open stream" << std::endl;
		return 1;
	}

	std::thread t(captureThread, std::ref(cap));
	
	cv::Mat frame;
	while (running) {
		if (frameQueue.try_pop(frame)) {
			cv::imshow("frame", frame);
		}
		if ((cv::waitKey(10) & 0xFF) == 'q') {
			running = false;
		}
	}

	t.join();
	cap.release();
	cv::destroyAllWindows();
	return 0;
}

// Local variables:
// c-basic-offset: 4
// tab-width: 4
// indent-tabs-mode: t
// End:
