#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <thread>
#include <chrono>

using namespace std::literals;

void background_work(std::stop_token stop_tkn, const int id, const std::string text, 
                     const std::chrono::milliseconds delay) 
{
	std::stop_callback exit_callback(stop_tkn, [id]()
    {
		std::cout << "Callback: Thread#" << id << " received stop request..." 
				  << "; THD#" << std::this_thread::get_id() << std::endl;
		std::this_thread::sleep_for(10s);
        std::cout << "Callback: Thread#" << id << " finished handling stop request..." << std::endl;
    });

    std::cout << "Thread#" << id << " started..." << std::endl;
	std::cout << "THD#" << std::this_thread::get_id() << " is doing some work..." << std::endl;

    for(const auto& letter : text)
    {
		if (stop_tkn.stop_requested())
		{
			std::this_thread::sleep_for(5s);
			std::cout << "Thread#" << id << " is stopping..." << std::endl;
			return;
		}
        std::cout << "Thread#" << id << " - " << letter << std::endl;
        std::this_thread::sleep_for(delay);
    }
}

int main()
{
	std::cout << "Main thread has started..." << std::endl;
	std::cout << "No of cores: " << std::max(1u, std::thread::hardware_concurrency()) << std::endl;

	std::stop_source stop_src;
	
	std::cout << "Main THD#" << std::this_thread::get_id() << " is doing some work..." << std::endl;
	
	//std::stop_token tkn = stop_src.get_token();
	// std::jthread thd_1{background_work, tkn, 1, "Hello 1!"s, 500ms};
	// std::jthread thd_2{background_work, stop_src.get_token(), 2, "Hello 2!"s, 700ms};

	std::jthread thd_1{background_work, 1, "Hello 1!"s, 500ms};
	std::jthread thd_2{background_work, 2, "Hello 2!"s, 700ms};

	std::this_thread::sleep_for(1s);

	auto stop_src_1 = thd_1.get_stop_source();
	stop_src_1.request_stop();

	std::this_thread::sleep_for(2s);

	std::cout << "Main thread has ended..."	<< std::endl;
}