import sys
import win32pipe
import win32file
import pywintypes

def start_pipe(symbol, timeframe):
    pipe_name = fr'\\.\pipe\SD_{symbol}_{timeframe}'
    print(f"[INFO] Creating pipe: {pipe_name}")

    while True:
        pipe = win32pipe.CreateNamedPipe(
            pipe_name,
            win32pipe.PIPE_ACCESS_INBOUND,
            win32pipe.PIPE_TYPE_MESSAGE |
            win32pipe.PIPE_READMODE_MESSAGE |
            win32pipe.PIPE_WAIT,
            win32pipe.PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            0,
            None
        )

        print(f"[INFO] Waiting for connection on {pipe_name} ...")

        try:
            win32pipe.ConnectNamedPipe(pipe, None)
            print(f"[CONNECTED] {pipe_name}")

            while True:
                result, data = win32file.ReadFile(pipe, 4096)
                message = data.decode("utf-8").strip()

                print(f"[DATA] {message}")

        except pywintypes.error as e:
            print(f"[DISCONNECTED] {pipe_name} | {e}")

        finally:
            win32file.CloseHandle(pipe)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python pipe_server.py <SYMBOL> <TIMEFRAME>")
        print("Example: python pipe_server.py NIFTY-FUT 5min")
        sys.exit(1)

    symbol = sys.argv[1]
    timeframe = sys.argv[2]

    start_pipe(symbol, timeframe)