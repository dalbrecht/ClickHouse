#include <DB/Common/Exception.h>
#include <DB/Common/setThreadName.h>
#include <DB/Common/CurrentMetrics.h>
#include <DB/IO/WriteHelpers.h>
#include <common/logger_useful.h>
#include <DB/Storages/MergeTree/BackgroundProcessingPool.h>

#include <random>

namespace DB
{


constexpr double BackgroundProcessingPool::sleep_seconds;
constexpr double BackgroundProcessingPool::sleep_seconds_random_part;


void BackgroundProcessingPool::TaskInfo::wake()
{
	if (removed)
		return;

	Poco::Timestamp current_time;

	{
		std::unique_lock<std::mutex> lock(pool.tasks_mutex);

		auto next_time_to_execute = iterator->first;
		TaskHandle this_task_handle = iterator->second;

		/// If this task was done nothing at previous time and it has to sleep, then cancel sleep time.
		if (next_time_to_execute > current_time)
			next_time_to_execute = current_time;

		pool.tasks.erase(iterator);
		iterator = pool.tasks.emplace(next_time_to_execute, this_task_handle);
	}

	/// Note that if all threads are currently do some work, this call will not wakeup any thread.
	pool.wake_event.notify_one();
}


BackgroundProcessingPool::BackgroundProcessingPool(int size_) : size(size_)
{
	LOG_INFO(&Logger::get("BackgroundProcessingPool"), "Create BackgroundProcessingPool with " << size << " threads");

	threads.resize(size);
	for (auto & thread : threads)
		thread = std::thread([this] { threadFunction(); });
}


int BackgroundProcessingPool::getCounter(const String & name)
{
	std::unique_lock<std::mutex> lock(counters_mutex);
	return counters[name];
}

BackgroundProcessingPool::TaskHandle BackgroundProcessingPool::addTask(const Task & task)
{
	TaskHandle res = std::make_shared<TaskInfo>(*this, task);

	Poco::Timestamp current_time;

	{
		std::unique_lock<std::mutex> lock(tasks_mutex);
		res->iterator = tasks.emplace(current_time, res);
	}

	wake_event.notify_all();

	return res;
}

void BackgroundProcessingPool::removeTask(const TaskHandle & task)
{
	if (task->removed.exchange(true))
		return;

	/// Wait for all execution of this task.
	{
		Poco::ScopedWriteRWLock wlock(task->rwlock);
	}

	{
		std::unique_lock<std::mutex> lock(tasks_mutex);
		tasks.erase(task->iterator);
	}
}

BackgroundProcessingPool::~BackgroundProcessingPool()
{
	try
	{
		shutdown = true;
		wake_event.notify_all();
		for (std::thread & thread : threads)
			thread.join();
	}
	catch (...)
	{
		tryLogCurrentException(__PRETTY_FUNCTION__);
	}
}


void BackgroundProcessingPool::threadFunction()
{
	setThreadName("BackgrProcPool");

	std::mt19937 rng(reinterpret_cast<intptr_t>(&rng));
	std::this_thread::sleep_for(std::chrono::duration<double>(std::uniform_real_distribution<double>(0, sleep_seconds_random_part)(rng)));

	while (!shutdown)
	{
		Counters counters_diff;
		bool done_work = false;
		TaskHandle task;

		try
		{
			Poco::Timestamp min_time;

			{
				std::unique_lock<std::mutex> lock(tasks_mutex);

				if (!tasks.empty())
				{
					for (const auto & time_handle : tasks)
					{
						if (!time_handle.second->removed)
						{
							min_time = time_handle.first;
							task = time_handle.second;
							break;
						}
					}
				}
			}

			if (shutdown)
				break;

			if (!task)
			{
				std::unique_lock<std::mutex> lock(tasks_mutex);
				wake_event.wait_for(lock,
					std::chrono::duration<double>(sleep_seconds
						+ std::uniform_real_distribution<double>(0, sleep_seconds_random_part)(rng)));
				continue;
			}

			/// No tasks ready for execution.
			Poco::Timestamp current_time;
			if (min_time > current_time)
			{
				std::unique_lock<std::mutex> lock(tasks_mutex);
				wake_event.wait_for(lock, std::chrono::microseconds(
					min_time - current_time + std::uniform_int_distribution<uint64_t>(0, sleep_seconds_random_part * 1000000)(rng)));
			}

			Poco::ScopedReadRWLock rlock(task->rwlock);

			if (task->removed)
				continue;

			{
				CurrentMetrics::Increment metric_increment{CurrentMetrics::BackgroundPoolTask};

				Context context(*this, counters_diff);
				done_work = task->function(context);
			}
		}
		catch (...)
		{
			tryLogCurrentException(__PRETTY_FUNCTION__);
		}

		/// Subtract counters backwards.
		if (!counters_diff.empty())
		{
			std::unique_lock<std::mutex> lock(counters_mutex);
			for (const auto & it : counters_diff)
				counters[it.first] -= it.second;
		}

		if (shutdown)
			break;

		/// If task has done work, it could be executed again immediately.
		/// If not, add delay before next run.
		Poco::Timestamp next_time_to_execute = Poco::Timestamp() + (done_work ? 0 : sleep_seconds * 1000000);

		{
			std::unique_lock<std::mutex> lock(tasks_mutex);

			if (task->removed)
				return;

			tasks.erase(task->iterator);
			task->iterator = tasks.emplace(next_time_to_execute, task);
		}
	}
}

}
