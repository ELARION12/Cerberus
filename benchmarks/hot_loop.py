# benchmarks/hot_loop.py
import os
import sys
import time
import resource
import gc

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../python"))

ITERATIONS = 50000
TOKEN_LIMIT_CHARS = 16384  # Roughly 4096 tokens

def get_memory_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0

def run_cerberus_hot_loop():
    print("=== Cerberus Hot Loop (50,000 Iterations) ===", flush=True)
    time.sleep(1.5)  # Pause for GIF recording

    gc.collect()
    mem_start = get_memory_mb()

    from cerberus_agent.agent import Agent
    from cerberus_agent._cerberus_core import AgentConfig

    config = AgentConfig()
    config.context_arena_capacity = 1024 * 1024
    config.context_scratch_capacity = 1024 * 1024
    
    agent = Agent(config=config)
    agent.context.add_system("System: Execute sustained processing loop.")

    # --- START BENCHMARK TIMER ---
    start_time = time.perf_counter()

    for i in range(ITERATIONS):
        agent.context.add_user(f"Task {i}: Calculate and return data parameters.")
        agent.context.add_assistant(f"Result {i}: Data processed successfully.")
        
        if i % 10 == 0:
            prompt_view = agent.context.build_prompt()

    end_time = time.perf_counter()
    # --- END BENCHMARK TIMER ---
    
    gc.collect()
    mem_end = get_memory_mb()

    total_time = end_time - start_time
    print(f"Total Time        : {total_time:.3f} seconds", flush=True)
    print(f"Throughput        : {ITERATIONS / total_time:.0f} iterations/sec", flush=True)
    print(f"Added Memory (RSS): {mem_end - mem_start:.2f} MB", flush=True)
    print("==============================================\n", flush=True)
    
    time.sleep(2.0)  # Pause to let viewer digest Cerberus throughput

def run_langchain_hot_loop():
    print("=== LangChain Hot Loop (50,000 Iterations) ===", flush=True)
    time.sleep(1.5)  # Pause for GIF recording

    gc.collect()
    mem_start = get_memory_mb()

    try:
        from langchain_core.messages import SystemMessage, HumanMessage, AIMessage
    except ImportError:
        print("[LangChain is not installed in the environment. Skipping LangChain benchmark.]\n", flush=True)
        return

    messages = [SystemMessage(content="System: Execute sustained processing loop.")]
    current_chars = len(messages[0].content)

    # --- START BENCHMARK TIMER ---
    start_time = time.perf_counter()

    for i in range(ITERATIONS):
        h_msg = HumanMessage(content=f"Task {i}: Calculate and return data parameters.")
        a_msg = AIMessage(content=f"Result {i}: Data processed successfully.")
        
        messages.append(h_msg)
        messages.append(a_msg)
        current_chars += len(h_msg.content) + len(a_msg.content)

        while current_chars > TOKEN_LIMIT_CHARS and len(messages) > 1:
            dropped = messages.pop(1)
            current_chars -= len(dropped.content)

        if i % 10 == 0:
            _ = "\n".join([f"[{m.type.upper()}]: {m.content}" for m in messages])

    end_time = time.perf_counter()
    # --- END BENCHMARK TIMER ---
    
    gc.collect()
    mem_end = get_memory_mb()

    total_time = end_time - start_time
    print(f"Total Time        : {total_time:.3f} seconds", flush=True)
    print(f"Throughput        : {ITERATIONS / total_time:.0f} iterations/sec", flush=True)
    print(f"Added Memory (RSS): {mem_end - mem_start:.2f} MB", flush=True)
    print("==============================================\n", flush=True)
    
    time.sleep(3.0)  # Hold final results before GIF loops

if __name__ == "__main__":
    if len(sys.argv) == 1:
        import subprocess
        subprocess.run([sys.executable, sys.argv[0], "--cerberus"])
        subprocess.run([sys.executable, sys.argv[0], "--langchain"])
    elif sys.argv[1] == "--cerberus":
        run_cerberus_hot_loop()
    elif sys.argv[1] == "--langchain":
        run_langchain_hot_loop()