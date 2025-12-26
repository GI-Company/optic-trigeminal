#pragma once

#include "types.h"
#include <map>
#include <memory>
#include <vector>

enum class TimeHorizon {
    IMMEDIATE = 0,
    SHORT_TERM = 1,
    MEDIUM_TERM = 2,
    LONG_TERM = 3
};

struct HorizonTask {
    std::string task_id;
    std::string goal;
    TimeHorizon horizon;
    std::vector<std::string> subtask_ids;
    std::string parent_task_id;
    int decomposition_depth;
    float completion_percentage;
    int64_t target_start_time;
    int64_t target_end_time;
    int64_t actual_start_time;
    int64_t actual_end_time;
    std::vector<std::string> dependencies;
    std::vector<std::string> milestones;
    std::map<std::string, float> resource_allocation;
    
    HorizonTask() : decomposition_depth(0), completion_percentage(0.0f),
                   target_start_time(0), target_end_time(0),
                   actual_start_time(0), actual_end_time(0) {}
};

struct MilestoneMarker {
    std::string milestone_id;
    std::string task_id;
    std::string description;
    int64_t planned_time;
    int64_t actual_time;
    bool completed;
    float progress;
    
    MilestoneMarker() : planned_time(0), actual_time(0), completed(false), progress(0.0f) {}
};

struct ExecutionSchedule {
    std::vector<HorizonTask> immediate_tasks;
    std::vector<HorizonTask> short_term_tasks;
    std::vector<HorizonTask> medium_term_tasks;
    std::vector<HorizonTask> long_term_tasks;
    std::vector<MilestoneMarker> milestones;
    int64_t schedule_created_at;
    
    ExecutionSchedule() : schedule_created_at(0) {}
};

class LongHorizonPlanner {
public:
    LongHorizonPlanner();
    
    HorizonTask create_horizon_task(const std::string& goal,
                                   TimeHorizon horizon,
                                   int64_t target_end_time,
                                   const std::string& parent_id = "");
    
    std::vector<HorizonTask> decompose_task(const std::string& task_id, int max_depth = 4);
    
    bool add_subtask(const std::string& parent_task_id,
                    const std::string& subtask_id);
    
    bool add_dependency(const std::string& dependent_task_id,
                       const std::string& required_task_id);
    
    bool mark_milestone(const std::string& task_id, const std::string& milestone_desc);
    
    ExecutionSchedule generate_execution_schedule(const std::vector<HorizonTask>& tasks);
    
    std::vector<HorizonTask> get_ready_tasks() const;
    
    void update_task_progress(const std::string& task_id, float completion_percentage);
    
    void mark_task_complete(const std::string& task_id);
    
    float estimate_total_progress() const;
    
    std::string get_timeline_visualization() const;
    
    std::vector<HorizonTask> get_tasks_by_horizon(TimeHorizon horizon) const;
    
    bool are_dependencies_satisfied(const std::string& task_id) const;
    
private:
    std::map<std::string, HorizonTask> task_map;
    std::map<std::string, std::vector<MilestoneMarker>> milestone_map;
    
    std::string generate_task_id();
    
    std::vector<std::string> decompose_goal(const std::string& goal, int depth);
    
    TimeHorizon estimate_required_horizon(const std::string& goal) const;
    
    int estimate_decomposition_depth(TimeHorizon horizon) const;
};
