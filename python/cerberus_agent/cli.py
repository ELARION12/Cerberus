"""
Simple CLI to run Cerberus from the terminal.

Usage:
    cerberus "list files in home directory"
    cerberus --name DevBot "check disk space"
"""

import argparse
import sys
from .agent import Agent
from ._log import set_verbose


def main():
    parser = argparse.ArgumentParser(
        prog="cerberus",
        description="Cerberus agent runtime"
    )
    parser.add_argument(
        "task",
        nargs="?",
        default=None,
        help="Task for the agent to execute"
    )
    parser.add_argument(
        "--name", default="Cerberus",
        help="Agent name (default: Cerberus)"
    )
    parser.add_argument(
        "--steps", type=int, default=10,
        help="Max steps (default: 10)"
    )
    parser.add_argument(
        "--threads", type=int, default=4,
        help="Thread count (default: 4)"
    )
    parser.add_argument(
        "--list-tools", action="store_true",
        help="List all available tools and exit"
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="Do not print the agent's progress, only the result"
    )
    parser.add_argument(
        "--version", action="store_true",
        help="Print version and exit"
    )

    args = parser.parse_args()

    if args.version:
        from . import __version__
        print(f"Cerberus v{__version__}")
        sys.exit(0)

    set_verbose(not args.quiet and not args.list_tools)

    agent = Agent(
        name=args.name,
        max_steps=args.steps,
        num_threads=args.threads
    )

    if args.list_tools:
        print(f"\n[{agent.name}] Available tools:")
        for t in agent.tool_names:
            print(f"  - {t}")
        print()
        sys.exit(0)

    if not args.task:
        print("Cerberus Interactive Mode (type 'exit' to quit)\n")
        while True:
            try:
                task = input(f"[{agent.name}] > ").strip()
                if task.lower() in ("exit", "quit", "q"):
                    break
                if task:
                    result = agent.run(task)
                    print(f"\nResult:\n{result}\n")
            except KeyboardInterrupt:
                print("\nBye")
                break
    else:
        result = agent.run(args.task)
        print(f"\nResult:\n{result}")


if __name__ == "__main__":
    main()