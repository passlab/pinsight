import sys
import time
from concurrent.futures import ThreadPoolExecutor

def do_work(thread_id):
    print(f"Thread {thread_id}: Hello! Starting my work...")
    # Simulate work with delay
    time.sleep(thread_id + 1)
    print(f"Thread {thread_id}: Done with my work!")

def main():
    print("Starting parallel work with 4 threads...\n")
    
    # Using thread pool to mimic C/OpenMP multi-threaded execution
    with ThreadPoolExecutor(max_workers=4) as executor:
        for i in range(4):
            executor.submit(do_work, i)
            
    print("\nAll threads finished!")

if __name__ == "__main__":
    main()
