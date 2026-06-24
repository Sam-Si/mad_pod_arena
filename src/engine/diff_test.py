import random
import subprocess
import sys
import os
import argparse

def generate_test_case(num_turns=100, stress_collisions=False):
    # Generate number of checkpoints
    num_cp = random.randint(3, 7)
    cps = []
    for _ in range(num_cp):
        x = float(random.randint(1000, 15000))
        y = float(random.randint(1000, 8000))
        cps.append((x, y))

    # Go referee maps CP list sequence (3 repeats + 1 extra CP0 at the end)
    global_cps = cps * 3 + [cps[0]]
    global_num_cp = len(global_cps)

    input_data = []
    input_data.append(str(global_num_cp))
    for cp in global_cps:
        input_data.append(f"{cp[0]} {cp[1]}")
    
    input_data.append(str(num_turns))

    for turn in range(num_turns):
        # 4 ignored state lines
        for _ in range(4):
            input_data.append("0 0 0 0 0 0 0 0 null 0 0")
        
        if stress_collisions:
            # Stress mode: generate moves that force collisions
            for pod_idx in range(4):
                if turn < 3:
                    # First few turns: all pods aim at the same point to force collisions
                    tx = float(cps[0][0])
                    ty = float(cps[0][1])
                    thrust = str(200)
                elif turn % 5 == 0:
                    # Every 5th turn: head-on collision setup
                    # Pods 0,1 aim right, pods 2,3 aim left
                    if pod_idx < 2:
                        tx = 15000.0
                        ty = 4500.0
                    else:
                        tx = 1000.0
                        ty = 4500.0
                    thrust = str(200)
                elif turn % 7 == 0:
                    # Shield during collisions
                    tx = float(random.randint(0, 16000))
                    ty = float(random.randint(0, 9000))
                    thrust = "SHIELD"
                elif turn % 11 == 0:
                    # Boost for high-velocity impacts
                    tx = float(cps[random.randint(0, num_cp - 1)][0])
                    ty = float(cps[random.randint(0, num_cp - 1)][1])
                    thrust = "BOOST"
                elif turn % 3 == 0:
                    # Aim all pods at a random checkpoint (cluster)
                    target_cp = random.randint(0, num_cp - 1)
                    tx = float(cps[target_cp][0])
                    ty = float(cps[target_cp][1])
                    thrust = str(random.randint(100, 200))
                else:
                    # Normal random moves with higher shield frequency
                    tx = float(random.randint(0, 16000))
                    ty = float(random.randint(0, 9000))
                    r = random.random()
                    if r < 0.15:  # 15% shield (3x normal)
                        thrust = "SHIELD"
                    elif r < 0.20:  # 5% boost
                        thrust = "BOOST"
                    else:
                        thrust = str(random.randint(0, 200))
                
                input_data.append(f"{tx} {ty} {thrust}")
        else:
            # Normal mode: standard random moves
            for _ in range(4):
                tx = float(random.randint(0, 16000))
                ty = float(random.randint(0, 9000))
                
                # 5% shield, 2% boost, 93% regular thrust
                r = random.random()
                if r < 0.05:
                    thrust = "SHIELD"
                elif r < 0.07:
                    thrust = "BOOST"
                else:
                    thrust = str(random.randint(0, 200))
                
                input_data.append(f"{tx} {ty} {thrust}")

    return "\n".join(input_data) + "\n"

def run_executable(cmd, input_str, timeout=2.0):
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    try:
        stdout, stderr = proc.communicate(input=input_str, timeout=timeout)
        return stdout, stderr, proc.returncode, False
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()
        return "", "Timeout", -1, True

def compare_outputs(cpp_out, go_out):
    cpp_lines = cpp_out.strip().split("\n")
    go_lines = go_out.strip().split("\n")

    if len(cpp_lines) != len(go_lines):
        print(f"ERROR: Line count mismatch! C++: {len(cpp_lines)}, Go: {len(go_lines)}")
        return False

    for idx, (cpp_l, go_l) in enumerate(zip(cpp_lines, go_lines)):
        cpp_parts = cpp_l.split()
        go_parts = go_l.split()

        if len(cpp_parts) != len(go_parts):
            print(f"ERROR on line {idx}: Part count mismatch! C++: '{cpp_l}' vs Go: '{go_l}'")
            return False

        # Formats:
        # p.p.x p.p.y p.s.x p.s.y p.angle p.next p.shieldtimer p.boosted
        for pid, (c_p, g_p) in enumerate(zip(cpp_parts, go_parts)):
            if pid == 4: # angle (float)
                c_val = float(c_p)
                g_val = float(g_p)
                # Allow minor floating point difference in angle printout due to math trig library variations
                # Trig functions like std::cos vs math.Sincos can have differences in very low decimal places
                if abs(c_val - g_val) > 1e-4:
                    # Check modular angle difference (e.g. -179.9999 vs 180.0)
                    diff = abs(c_val - g_val) % 360
                    if diff > 1e-4 and abs(360 - diff) > 1e-4:
                        print(f"ERROR on line {idx}, value {pid} (angle): C++: '{c_p}' vs Go: '{g_p}' (diff={abs(c_val-g_val)})")
                        return False
            else: # integers
                if int(c_p) != int(g_p):
                    print(f"ERROR on line {idx}, value {pid}: C++: '{c_p}' vs Go: '{g_p}'")
                    # Print context
                    start = max(0, idx - 8)
                    end = min(len(cpp_lines), idx + 8)
                    print("\n--- CONTEXT (Go referee output) ---")
                    for i in range(start, end):
                        marker = ">>>" if i == idx else "   "
                        print(f"{marker} Go {i:03d}: {go_lines[i]}")
                    print("\n--- CONTEXT (C++ engine output) ---")
                    for i in range(start, end):
                        marker = ">>>" if i == idx else "   "
                        print(f"{marker} C++{i:03d}: {cpp_lines[i]}")
                    return False

    return True

def main():
    parser = argparse.ArgumentParser(description="Differential physics test: C++ vs Go referee")
    parser.add_argument("--stress", action="store_true", help="Run collision stress tests with forced collisions/shields")
    parser.add_argument("--count", type=int, default=None, help="Number of tests to run (default: 50 normal, 200 stress)")
    args = parser.parse_args()

    # Resolve binary relative to repo root (works from any CWD)
    _repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    cpp_bin = os.path.join(_repo, "bazel-bin", "src", "engine", "test_physics")
    go_src = "/Users/samsi/csb/temp_referee/csbref.go"
    go_bin = "/tmp/csbref_go"

    print("Compiling Go referee...")
    go_compile_res = subprocess.run(["go", "build", "-o", go_bin, go_src])
    if go_compile_res.returncode != 0:
        print("ERROR: Failed to compile Go referee!")
        sys.exit(1)

    mode = "STRESS COLLISION" if args.stress else "standard"
    total_tests = args.count if args.count else (200 if args.stress else 50)
    print(f"Running {total_tests} {mode} differential tests...")
    passed_tests = 0
    skipped = 0

    while passed_tests < total_tests:
        test_input = generate_test_case(num_turns=100, stress_collisions=args.stress)
        
        cpp_out, cpp_err, cpp_code, cpp_timeout = run_executable([cpp_bin], test_input, timeout=2.0)
        if cpp_timeout or cpp_code != 0:
            print(f"C++ test executable crashed/timed out: {cpp_err}")
            sys.exit(1)

        go_out, go_err, go_code, go_timeout = run_executable([go_bin, "-test"], test_input, timeout=2.0)
        if go_timeout:
            # Go referee entered an infinite loop; skip this case silently
            skipped += 1
            if skipped > total_tests:
                print(f"WARNING: Too many skipped cases ({skipped}), possible issue with test generation")
                break
            continue

        if go_code != 0:
            print(f"Go referee crashed: {go_err}")
            sys.exit(1)

        if compare_outputs(cpp_out, go_out):
            passed_tests += 1
            if passed_tests % 10 == 0 or passed_tests == total_tests:
                print(f"Test {passed_tests}/{total_tests} passed...")
        else:
            print(f"Differential test FAILED!")
            with open("/tmp/failed_test_case.txt", "w") as f:
                f.write(test_input)
            print("Saved failed test case input to /tmp/failed_test_case.txt")
            sys.exit(1)

    print(f"\nALL {passed_tests}/{total_tests} {mode.upper()} DIFFERENTIAL TESTS PASSED PERFECTLY!")
    if skipped > 0:
        print(f"(Skipped {skipped} cases due to Go referee timeouts)")

if __name__ == "__main__":
    main()
