#include <iostream>
#include <opencv2/opencv.hpp>

#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;
using namespace cv;

static const string BOUNDARY = "frame";
static const uint16_t SERVER_PORT = 8080;

// Simple thread-safe client list
vector<int> clients;
mutex clientsMutex;
atomic<bool> serverRunning{true};

static bool send_all(int sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// Serve basic index page or stream
void server_thread(uint16_t port) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // accessible on local Wi-Fi
    addr.sin_port = htons(port);

    if (::bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv);
        return;
    }

    if (listen(srv, 10) < 0) {
        perror("listen");
        close(srv);
        return;
    }

    while (serverRunning.load()) {
        sockaddr_in cliAddr;
        socklen_t cliLen = sizeof(cliAddr);
        int cli = accept(srv, (sockaddr*)&cliAddr, &cliLen);
        if (cli < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        // set low-latency socket options for this client
        int flag = 1;
        setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms send timeout
        setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // Read simple request (blocking until headers read)
        string req;
        char buf[1024];
        ssize_t r = recv(cli, buf, sizeof(buf) - 1, 0);
        if (r <= 0) { close(cli); continue; }
        buf[r] = '\0';
        req = string(buf);

        // extract path
        string path = "/";
        size_t p0 = req.find(' ');
        if (p0 != string::npos) {
            size_t p1 = req.find(' ', p0 + 1);
            if (p1 != string::npos && p1 > p0 + 1) {
                path = req.substr(p0 + 1, p1 - (p0 + 1));
            }
        }

        if (path == "/" || path == "/index.html") {
            string page =
                "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
                "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head>"
                "<body style=\"margin:0;padding:0;background:transparent;overflow:hidden;\">"
                "<img src=\"/stream\" style=\"width:100vw;height:100vh;object-fit:cover;display:block;\">"
                "</body></html>";
            send_all(cli, page.c_str(), page.size());
            close(cli);
            continue;
        }

        if (path == "/stream") {
            // send MJPEG headers
            string hdr =
                "HTTP/1.0 200 OK\r\n"
                "Cache-Control: no-cache\r\n"
                "Pragma: no-cache\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=" + BOUNDARY + "\r\n\r\n";
            if (!send_all(cli, hdr.c_str(), hdr.size())) { close(cli); continue; }

            // Add to clients list
            {
                lock_guard<mutex> lk(clientsMutex);
                clients.push_back(cli);
            }
            // NOTE: keep socket open; main loop will push frames
            continue;
        }

        // unknown path
        string notfound = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
        send_all(cli, notfound.c_str(), notfound.size());
        close(cli);
    }

    close(srv);
}

int main() {
    // Local camera capture
    VideoCapture cap(0);
    if (!cap.isOpened()) {
        cerr << "Error: Cannot open camera" << endl;
        return -1;
    }

    Mat frame;
    const int targetWidth = 640;
    const int targetHeight = 480;

    // Start HTTP MJPEG server on an unprivileged port.
    thread srv(server_thread, SERVER_PORT);

    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        if (cv::waitKey(1) == 'q') break;

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
        vector<Vec3f> circles;
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

        // JPEG encode with lower quality for lower latency
        vector<uchar> buf;
        vector<int> params = {IMWRITE_JPEG_QUALITY, 60};
        imencode(".jpg", outFrame, buf, params);

        // Prepare MJPEG part
        string header = "--" + BOUNDARY + "\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: ";
        header += to_string(buf.size());
        header += "\r\n\r\n";

        // Send to all clients
        {
            lock_guard<mutex> lk(clientsMutex);
            for (auto it = clients.begin(); it != clients.end(); ) {
                int cli = *it;
                bool ok = send_all(cli, header.c_str(), header.size());
                if (ok) ok = send_all(cli, reinterpret_cast<char*>(buf.data()), buf.size());
                if (ok) {
                    const char tail[] = "\r\n";
                    ok = send_all(cli, tail, 2);
                }
                if (!ok) {
                    close(cli);
                    it = clients.erase(it);
                } else ++it;
            }
        }
    }

    // Shutdown
    serverRunning.store(false);
    // close all clients
    {
        lock_guard<mutex> lk(clientsMutex);
        for (int c: clients) close(c);
        clients.clear();
    }

    cap.release();
    // ensure server thread ends
    if (srv.joinable()) srv.join();

    return 0;
}
