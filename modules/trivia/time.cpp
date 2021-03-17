#include <algorithm>
#include <iomanip>
#include <ctime>
#include <string>
#include <chrono>

using namespace std::literals;

double time_f()
{
	using namespace std::chrono;
	auto tp = system_clock::now() + 0ns;
	return tp.time_since_epoch().count() / 1000000000.0;
}

