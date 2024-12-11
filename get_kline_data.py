import asyncio
import websockets
import json

async def stream_klines():
    uri = "wss://stream.binance.com:9443/ws/btcusdt@kline_1m"

    async with websockets.connect(uri) as websocket:
        print("Connected to Binance WebSocket")

        while True:
            try:
                # Receive message
                message = await websocket.recv()
                print(message)
                data = json.loads(message)

                # Extract kline data
                kline = data["k"]
                print(f"Time: {kline['t']}, Open: {kline['o']}, Close: {kline['c']}, High: {kline['h']}, Low: {kline['l']}")
            except Exception as e:
                print(f"Error: {e}")
                break

# Run the WebSocket client
asyncio.run(stream_klines())
