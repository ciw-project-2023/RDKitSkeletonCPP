/*
 * Copyright 2023 CoAler Group, all rights reserved.
 */

#include "OutputWriter.hpp"

#include <GraphMol/FileParsers/MolWriters.h>
#include <GraphMol/RWMol.h>
#include <GraphMol/SmilesParse/SmilesWrite.h>
#include <spdlog/spdlog.h>

namespace coaler::io {

    void OutputWriter::writeSDF(const std::string &file_path, const coaler::multialign::MultiAlignerResult &result) {
        std::ofstream output_file(file_path);
        if (!output_file.is_open()) {
            spdlog::error("Cannot open file: {}", file_path);
            return;
        }

        const boost::shared_ptr<RDKit::SDWriter> sdf_writer(new RDKit::SDWriter(&output_file, false));
        for (const auto &[ligand_id, pose_id] : result.poseIDsByLigandID) {
            auto entry = result.inputLigands.at(ligand_id);
            sdf_writer->write(entry.getMolecule(), pose_id);
        }
    }
}  // namespace coaler::io
