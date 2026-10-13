#include "TaskLoad.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr UBaseType_t kMaxTasks = 40;

struct TaskSnapshot {
  TaskStatus_t tasks[kMaxTasks];
  UBaseType_t count = 0;
  configRUN_TIME_COUNTER_TYPE total = 0;
};

void snapshot(TaskSnapshot &shot) {
  shot.count = uxTaskGetSystemState(shot.tasks, kMaxTasks, &shot.total);
}

configRUN_TIME_COUNTER_TYPE counterBefore(const TaskSnapshot &before, TaskHandle_t task) {
  for (UBaseType_t i = 0; i < before.count; ++i) {
    if (before.tasks[i].xHandle == task) return before.tasks[i].ulRunTimeCounter;
  }
  return 0;
}

// The total is elapsed time, so a share is of one core.
void printDifference(const TaskSnapshot &before, const TaskSnapshot &after) {
  const double total = static_cast<double>(after.total - before.total);
  for (UBaseType_t i = 0; i < after.count; ++i) {
    const TaskStatus_t &task = after.tasks[i];
    const double share =
        100.0 * (task.ulRunTimeCounter - counterBefore(before, task.xHandle)) / total;
    if (share < 0.5) continue;
    Serial.printf("[load] %-16s core %2d prio %2u  %5.1f %%\n", task.pcTaskName,
                  static_cast<int>(task.xCoreID), static_cast<unsigned>(task.uxCurrentPriority),
                  share);
  }
}

}  // namespace

void printTaskLoad(uint32_t ms) {
  // ~3 KB each: static, not on the loop task's stack.
  static TaskSnapshot before;
  static TaskSnapshot after;
  snapshot(before);
  delay(ms);
  snapshot(after);
  printDifference(before, after);
}
