#include <chrono>
namespace legit
{
  class CpuProfiler
  {
  public:
    CpuProfiler()
    {
      frameIndex = 0;
    }
    size_t StartTask(std::string taskName, uint32_t taskColor)
    {
      legit::ProfilerTask task;
      task.color = taskColor;
      task.name = taskName;
      task.startTime = GetCurrFrameTimeSeconds();
      task.endTime = -1.0;
      size_t taskId = profilerTasks.size();
      profilerTasks.push_back(task);
      return taskId;
    }
    ProfilerTask EndTask(size_t taskId)
    {
      assert(profilerTasks.size() == taskId + 1 && profilerTasks.back().endTime < 0.0);
      profilerTasks.back().endTime = GetCurrFrameTimeSeconds();
      return profilerTasks.back();
    }

    size_t StartFrame()
    {
      profilerTasks.clear();
      frameStartTime = hrc::now();
      return frameIndex;
    }
    void EndFrame(size_t frameId)
    {
      assert(frameId == frameIndex);
      frameIndex++;
    }
    const std::vector<ProfilerTask> &GetProfilerTasks()
    {
      return profilerTasks;
    }
  private:
    double GetCurrFrameTimeSeconds()
    {
      return double(std::chrono::duration_cast<std::chrono::microseconds>(hrc::now() - frameStartTime).count()) / 1e6;
    }

    struct TaskHandleInfo
    {
      TaskHandleInfo(CpuProfiler *_profiler, size_t _taskId)
      {
        this->profiler = _profiler;
        this->taskId = _taskId;
      }
      void Reset()
      {
        profiler->EndTask(taskId);
      }
      CpuProfiler *profiler;
      size_t taskId;
    };
    struct FrameHandleInfo
    {
      FrameHandleInfo(CpuProfiler *_profiler, size_t _frameId)
      {
        this->profiler = _profiler;
        this->frameId = _frameId;
      }
      void Reset()
      {
        profiler->EndFrame(frameId);
      }
      CpuProfiler *profiler;
      size_t frameId;
    };
  public:
    using ScopedTask = UniqueHandle<TaskHandleInfo, CpuProfiler>;
    ScopedTask StartScopedTask(std::string taskName, uint32_t taskColor)
    {
      return ScopedTask(TaskHandleInfo(this, StartTask(taskName, taskColor)), true);
    }
    using ScopedFrame = UniqueHandle<FrameHandleInfo, CpuProfiler>;
    ScopedFrame StartScopedFrame()
    {
      return ScopedFrame(FrameHandleInfo(this, StartFrame()), true);
    }
  private:
    using hrc = std::chrono::high_resolution_clock;
    size_t frameIndex;
    std::vector<legit::ProfilerTask> profilerTasks;
    hrc::time_point frameStartTime;
    friend struct ScopedTask;
  };
}