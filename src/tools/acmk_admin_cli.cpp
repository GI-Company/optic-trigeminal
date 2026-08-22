#include "admin_cli.h"
#include "training_orchestrator.h"
#include "checkpoint_persistence.h"
#include "artifact_persistence.h"
#include "data_pipeline.h"
#include "training_stages.h"
#include "vfs_manager.h"
#include <iostream>
#include <memory>

int main() {
    auto vfs = std::make_unique<VFSManager>();
    auto data_pipeline = std::make_unique<DataPipeline>();
    auto training_controller = std::make_unique<TrainingController>(vfs.get(), data_pipeline.get());
    auto checkpoint_persistence = std::make_unique<CheckpointPersistence>("data/checkpoints");
    auto training_orchestrator = std::make_unique<TrainingOrchestrator>(
        training_controller.get(),
        data_pipeline.get(),
        vfs.get()
    );
    auto artifact_persistence = std::make_unique<ArtifactPersistenceManager>("data/artifacts");
    
    auto cli = std::make_unique<AdminCLI>(
        training_orchestrator.get(),
        checkpoint_persistence.get(),
        artifact_persistence.get(),
        data_pipeline.get()
    );
    
    cli->run_interactive_mode();
    
    return 0;
}
