#include "long_horizon_planner.h"
#include <chrono>
#include <algorithm>
#include <sstream>

LongHorizonPlanner::LongHorizonPlanner() {}

std::string LongHorizonPlanner::generate_task_id() {
    static int counter = 0;
    return "horizon_" + std::to_string(counter++) + "_" + 
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
}

HorizonTask LongHorizonPlanner::create_horizon_task(const std::string& goal,
                                                   TimeHorizon horizon,
                                                   int64_t target_end_time,
                                                   const std::string& parent_id) {
    HorizonTask task;
    task.task_id = generate_task_id();
    task.goal = goal;
    task.horizon = horizon;
    task.parent_task_id = parent_id;
    task.target_end_time = target_end_time;
    task.target_start_time = std::chrono::system_clock::now().time_since_epoch().count();
    task.decomposition_depth = 0;
    task.completion_percentage = 0.0f;
    task.actual_start_time = 0;
    task.actual_end_time = 0;
    
    task_map[task.task_id] = task;
    
    if (!parent_id.empty() && task_map.count(parent_id)) {
        task_map[parent_id].subtask_ids.push_back(task.task_id);
    }
    
    return task;
}

std::vector<HorizonTask> LongHorizonPlanner::decompose_task(const std::string& task_id, int max_depth) {
    std::vector<HorizonTask> decomposed;
    
    auto it = task_map.find(task_id);
    if (it == task_map.end()) return decomposed;
    
    auto task = it->second;
    
    if (task.decomposition_depth >= max_depth) {
        return decomposed;
    }
    
    std::vector<std::string> goals = decompose_goal(task.goal, task.decomposition_depth);
    
    for (size_t i = 0; i < goals.size(); ++i) {
        TimeHorizon sub_horizon = task.horizon;
        
        if (task.horizon == TimeHorizon::LONG_TERM && i % 3 == 0) {
            sub_horizon = TimeHorizon::MEDIUM_TERM;
        } else if (task.horizon == TimeHorizon::MEDIUM_TERM && i % 2 == 0) {
            sub_horizon = TimeHorizon::SHORT_TERM;
        } else if (task.horizon == TimeHorizon::SHORT_TERM) {
            sub_horizon = TimeHorizon::IMMEDIATE;
        }
        
        int64_t sub_deadline = task.target_end_time - 
                              (goals.size() - i) * (task.target_end_time - task.target_start_time) / goals.size();
        
        HorizonTask subtask = create_horizon_task(goals[i], sub_horizon, sub_deadline, task_id);
        subtask.decomposition_depth = task.decomposition_depth + 1;
        
        task_map[subtask.task_id] = subtask;
        task.subtask_ids.push_back(subtask.task_id);
        decomposed.push_back(subtask);
    }
    
    task_map[task_id] = task;
    return decomposed;
}

bool LongHorizonPlanner::add_subtask(const std::string& parent_task_id,
                                   const std::string& subtask_id) {
    auto parent_it = task_map.find(parent_task_id);
    auto subtask_it = task_map.find(subtask_id);
    
    if (parent_it == task_map.end() || subtask_it == task_map.end()) {
        return false;
    }
    
    parent_it->second.subtask_ids.push_back(subtask_id);
    subtask_it->second.parent_task_id = parent_task_id;
    
    return true;
}

bool LongHorizonPlanner::add_dependency(const std::string& dependent_task_id,
                                       const std::string& required_task_id) {
    auto dep_it = task_map.find(dependent_task_id);
    if (dep_it == task_map.end()) return false;
    
    dep_it->second.dependencies.push_back(required_task_id);
    return true;
}

bool LongHorizonPlanner::mark_milestone(const std::string& task_id, const std::string& milestone_desc) {
    auto task_it = task_map.find(task_id);
    if (task_it == task_map.end()) return false;
    
    MilestoneMarker milestone;
    static int counter = 0;
    milestone.milestone_id = "milestone_" + std::to_string(counter++);
    milestone.task_id = task_id;
    milestone.description = milestone_desc;
    milestone.planned_time = task_it->second.target_end_time;
    milestone.completed = false;
    milestone.progress = 0.0f;
    
    task_it->second.milestones.push_back(milestone.milestone_id);
    milestone_map[task_id].push_back(milestone);
    
    return true;
}

ExecutionSchedule LongHorizonPlanner::generate_execution_schedule(const std::vector<HorizonTask>& tasks) {
    ExecutionSchedule schedule;
    schedule.schedule_created_at = std::chrono::system_clock::now().time_since_epoch().count();
    
    for (const auto& task : tasks) {
        switch (task.horizon) {
            case TimeHorizon::IMMEDIATE:
                schedule.immediate_tasks.push_back(task);
                break;
            case TimeHorizon::SHORT_TERM:
                schedule.short_term_tasks.push_back(task);
                break;
            case TimeHorizon::MEDIUM_TERM:
                schedule.medium_term_tasks.push_back(task);
                break;
            case TimeHorizon::LONG_TERM:
                schedule.long_term_tasks.push_back(task);
                break;
        }
        
        auto milestone_it = milestone_map.find(task.task_id);
        if (milestone_it != milestone_map.end()) {
            for (const auto& milestone : milestone_it->second) {
                schedule.milestones.push_back(milestone);
            }
        }
    }
    
    std::sort(schedule.immediate_tasks.begin(), schedule.immediate_tasks.end(),
             [](const HorizonTask& a, const HorizonTask& b) {
                 return a.target_start_time < b.target_start_time;
             });
    
    return schedule;
}

std::vector<HorizonTask> LongHorizonPlanner::get_ready_tasks() const {
    std::vector<HorizonTask> ready;
    
    for (const auto& task_pair : task_map) {
        const auto& task = task_pair.second;
        
        if (task.completion_percentage >= 100.0f) continue;
        
        bool deps_satisfied = true;
        for (const auto& dep_id : task.dependencies) {
            auto dep_it = task_map.find(dep_id);
            if (dep_it != task_map.end() && dep_it->second.completion_percentage < 100.0f) {
                deps_satisfied = false;
                break;
            }
        }
        
        if (deps_satisfied) {
            ready.push_back(task);
        }
    }
    
    return ready;
}

void LongHorizonPlanner::update_task_progress(const std::string& task_id, float completion_percentage) {
    auto it = task_map.find(task_id);
    if (it != task_map.end()) {
        it->second.completion_percentage = std::min(100.0f, completion_percentage);
    }
}

void LongHorizonPlanner::mark_task_complete(const std::string& task_id) {
    auto it = task_map.find(task_id);
    if (it != task_map.end()) {
        it->second.completion_percentage = 100.0f;
        it->second.actual_end_time = std::chrono::system_clock::now().time_since_epoch().count();
    }
}

float LongHorizonPlanner::estimate_total_progress() const {
    if (task_map.empty()) return 0.0f;
    
    float total_progress = 0.0f;
    for (const auto& task_pair : task_map) {
        total_progress += task_pair.second.completion_percentage;
    }
    
    return total_progress / task_map.size();
}

std::string LongHorizonPlanner::get_timeline_visualization() const {
    std::stringstream ss;
    ss << "=== Long-Horizon Plan Timeline ===\n\n";
    
    if (!task_map.empty()) {
        ss << "IMMEDIATE TASKS:\n";
        for (const auto& task_pair : task_map) {
            if (task_pair.second.horizon == TimeHorizon::IMMEDIATE) {
                ss << "  [" << static_cast<int>(task_pair.second.completion_percentage) << "%] " 
                   << task_pair.second.goal << "\n";
            }
        }
        
        ss << "\nSHORT-TERM TASKS:\n";
        for (const auto& task_pair : task_map) {
            if (task_pair.second.horizon == TimeHorizon::SHORT_TERM) {
                ss << "  [" << static_cast<int>(task_pair.second.completion_percentage) << "%] " 
                   << task_pair.second.goal << "\n";
            }
        }
        
        ss << "\nMEDIUM-TERM TASKS:\n";
        for (const auto& task_pair : task_map) {
            if (task_pair.second.horizon == TimeHorizon::MEDIUM_TERM) {
                ss << "  [" << static_cast<int>(task_pair.second.completion_percentage) << "%] " 
                   << task_pair.second.goal << "\n";
            }
        }
        
        ss << "\nLONG-TERM TASKS:\n";
        for (const auto& task_pair : task_map) {
            if (task_pair.second.horizon == TimeHorizon::LONG_TERM) {
                ss << "  [" << static_cast<int>(task_pair.second.completion_percentage) << "%] " 
                   << task_pair.second.goal << "\n";
            }
        }
    }
    
    ss << "\nOverall Progress: " << estimate_total_progress() << "%\n";
    
    return ss.str();
}

std::vector<HorizonTask> LongHorizonPlanner::get_tasks_by_horizon(TimeHorizon horizon) const {
    std::vector<HorizonTask> result;
    
    for (const auto& task_pair : task_map) {
        if (task_pair.second.horizon == horizon) {
            result.push_back(task_pair.second);
        }
    }
    
    return result;
}

bool LongHorizonPlanner::are_dependencies_satisfied(const std::string& task_id) const {
    auto it = task_map.find(task_id);
    if (it == task_map.end()) return false;
    
    for (const auto& dep_id : it->second.dependencies) {
        auto dep_it = task_map.find(dep_id);
        if (dep_it == task_map.end() || dep_it->second.completion_percentage < 100.0f) {
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> LongHorizonPlanner::decompose_goal(const std::string& goal, int depth) {
    std::vector<std::string> subgoals;
    
    if (depth == 0) {
        subgoals.push_back("Analyze " + goal);
        subgoals.push_back("Plan approach for " + goal);
        subgoals.push_back("Execute " + goal);
        subgoals.push_back("Verify " + goal);
    } else if (depth == 1) {
        subgoals.push_back("Step 1: Prepare for " + goal);
        subgoals.push_back("Step 2: Execute " + goal);
        subgoals.push_back("Step 3: Integrate " + goal);
    } else {
        subgoals.push_back("Subtask A for " + goal);
        subgoals.push_back("Subtask B for " + goal);
    }
    
    return subgoals;
}

TimeHorizon LongHorizonPlanner::estimate_required_horizon(const std::string& goal) const {
    if (goal.find("immediate") != std::string::npos ||
        goal.find("urgent") != std::string::npos) {
        return TimeHorizon::IMMEDIATE;
    }
    if (goal.find("quick") != std::string::npos ||
        goal.find("fast") != std::string::npos) {
        return TimeHorizon::SHORT_TERM;
    }
    if (goal.find("plan") != std::string::npos ||
        goal.find("prepare") != std::string::npos) {
        return TimeHorizon::MEDIUM_TERM;
    }
    return TimeHorizon::LONG_TERM;
}

int LongHorizonPlanner::estimate_decomposition_depth(TimeHorizon horizon) const {
    switch (horizon) {
        case TimeHorizon::IMMEDIATE: return 1;
        case TimeHorizon::SHORT_TERM: return 2;
        case TimeHorizon::MEDIUM_TERM: return 3;
        case TimeHorizon::LONG_TERM: return 4;
        default: return 2;
    }
}
