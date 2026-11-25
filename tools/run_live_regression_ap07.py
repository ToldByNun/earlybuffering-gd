"""Compile and run the standalone Live regression gate, retaining every result."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


class LiveRegressionGate:
    @staticmethod
    def run():
        root = Path(__file__).resolve().parent.parent
        output = Path(tempfile.mkdtemp(prefix="earlybuffer-ap07-"))
        common = ["src/core/assist/contracts/live_plan_contracts.cpp",
                  "src/core/assist/contracts/assist_verdict.cpp"]
        cases = {
            "ship_line_policy": ["src/core/assist/flight/ship_line_policy.cpp"],
            "flight_intent": [
                "src/core/assist/flight/flight_intent.cpp",
                "src/core/assist/flight/flight_measurement.cpp",
                "src/core/assist/flight/wave_pattern_policy.cpp",
            ],
            "flight_measurement": ["src/core/assist/flight/flight_measurement.cpp"],
            "input_reconciler": ["src/core/assist/input/input_reconciler.cpp"],
            "completed_trigger_collection": ["src/core/trigger_world/completed_trigger_collection.cpp"],
            "settings_popup_policy": ["src/ui/settings/settings_popup_policy.cpp"],
            "live_rescue_policy": ["src/core/assist/contracts/live_rescue_policy.cpp",
                                   "src/core/assist/contracts/live_rescue_search.cpp",
                                   "src/core/assist/contracts/live_horizon_controller.cpp"],
            "input_search_policy": ["src/core/assist/contracts/input_search_policy.cpp"],
            "hybrid_model": ["src/core/learning/hybrid_model.cpp", "src/core/learning/hybrid_observation.cpp"],
            "neural_model": ["src/core/learning/neural_model.cpp"],
            "neural_features": [
                "src/core/learning/neural_model.cpp",
                "src/core/learning/neural_grid.cpp",
                "src/core/learning/neural_features.cpp",
                "src/core/learning/neural_replay.cpp",
                "src/core/learning/neural_supervised.cpp",
                "src/core/learning/neural_bootstrap.cpp",
            ],
            "neural_learning": [
                "src/core/learning/neural_model.cpp",
                "src/core/learning/neural_replay.cpp",
                "src/core/learning/neural_supervised.cpp",
                "src/core/learning/neural_grid.cpp",
                "src/core/learning/neural_features.cpp",
                "src/core/learning/neural_bootstrap.cpp",
            ],
            "assist_mode_migration": ["src/config/assist_mode_migration.cpp"],
            "config_keys": ["src/core/bundle/config_bundle_keys.cpp"],
            "wave_input_window": ["src/core/assist/contracts/live_rescue_policy.cpp",
                                  "src/core/assist/contracts/live_rescue_search.cpp",
                                  "src/core/assist/contracts/miss_assist_contracts.cpp",
                                  "src/core/assist/contracts/assist_verdict.cpp"],
            "adaptive_parity_smoke": common + ["src/core/sim/planning/search/adaptive/adaptive_parity_policy.cpp"],
            "fair_depth_ap1": common + ["src/dev/evaluation/dev_reject_ledger.cpp",
                                       "src/dev/evaluation/attribution/dev_attribution.cpp",
                                       "src/dev/evaluation/validation/dev_reject_checks.cpp"],
            "frame_plan_budget": ["src/core/assist/contracts/frame_plan_budget.cpp"],
            "ball_landing": ["src/core/sim/planning/ball_landing_tracker.cpp"],
            "orb_continuation": ["src/core/sim/planning/orb_continuation.cpp"],
            "orb_probe_order": ["src/core/assist/contracts/orb_probe_order.cpp"],
            "selected_tick_guard": ["src/core/assist/contracts/selected_tick_guard.cpp"],
            "subtick_timing": ["src/core/assist/contracts/subtick_timing.cpp"],
            "spider_continuation": ["src/core/sim/planning/spider_continuation.cpp"],
            "capture_index": ["src/core/sim/world/capture_index_key.cpp"],
            "simulation_window": ["src/core/sim/world/simulation_window.cpp"],
            "trigger_motion": ["src/core/trigger_world/trigger_motion_anchor.cpp"],
            "cube_landing": ["src/core/sim/planning/cube_landing_tracker.cpp"],
            "cube_rescue": [],
            "ufo_rescue": [],
        }
        results = []
        for name, sources in cases.items():
            executable = output / (name + ".exe")
            command = ["g++", "-std=c++23", "tools/test_" + name + ".cpp", *sources,
                       "-I", "build/_deps/fmt-src/include", "-DFMT_HEADER_ONLY", "-o", str(executable)]
            built = subprocess.run(command, cwd=root, capture_output=True, text=True)
            ran = subprocess.run([str(executable)], cwd=root, capture_output=True, text=True) if built.returncode == 0 else built
            results.append(dict(name=name, passed=ran.returncode == 0,
                                output=ran.stdout + ran.stderr, buildExit=built.returncode))
            print(name, "PASS" if results[-1]["passed"] else "FAIL", flush=True)
        for name in ("test_transition_work.py", "test_wave_ghost_input.py", "test_shadow_collision_window.py", "test_shadow_section_cleanup.py", "test_owned_object_walk.py",
                     "test_recorded_misses_ap01.py", "test_policy_registration_ap06.py",
                     "test_real_assist_score.py", "test_real_assist_smoke.py",
                     "test_real_assist_dense.py", "build_neural_dataset.py",
                     "build_neural_prior.py",
                     "test_geode_package_check.py", "test_bundle_geode.py", "check_cpp_style.py"):
            ran = subprocess.run([sys.executable, "tools/" + name], cwd=root, capture_output=True, text=True)
            results.append(dict(name=name, passed=ran.returncode == 0, output=ran.stdout + ran.stderr))
            print(name, "PASS" if results[-1]["passed"] else "FAIL", flush=True)
        report = output / "results.json"
        report.write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(report)
        return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(LiveRegressionGate.run())
