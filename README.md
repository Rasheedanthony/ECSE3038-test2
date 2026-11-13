# ECSE3038 Test 2: the bathroom fan

An ESP32 in Wokwi reads a DHT22's humidity every 10 s and POSTs it to a FastAPI + MongoDB Atlas API, using its own API key. The API stores each fan's mode (`auto`, `on`, `off`) and humidity limit, and pushes them to the board over MQTT (retained) on `ecse3038/<name>/fan/settings`. The board drives a fan LED from those settings and reports `on`/`off` on `ecse3038/<name>/fan/state`, with `online`/`offline` (last will) on `ecse3038/<name>/status`.

## Folders

- `api/`: the FastAPI app (`app.py`, `requirements.txt`). Needs a `.env`, never committed.
- `board/`: the PlatformIO project for the ESP32 (`src/main.cpp`, `diagram.json`, `wokwi.toml`). Needs `include/secrets.h`, never committed.

## Run the API (on the VM)

```bash
git clone https://github.com/<you>/ECSE3038-test2.git
cd ECSE3038-test2/api
python3 -m venv venv
source venv/bin/activate
python -m pip install -r requirements.txt
nano .env
uvicorn app:app --host 127.0.0.1 --port 8000
```

`.env` holds:

```
MONGODB_URI=mongodb+srv://...
MQTT_NAME=<name>
```

Routes: `POST/GET /fans`, `GET /fans/{mac}`, `POST/GET /readings`, `PUT /fans/{mac}/settings`, `DELETE /fans/{mac}`. Try them at `http://<server>/docs`.

## Run the board (Wokwi in VS Code)

1. Open the `board` folder in VS Code with the PlatformIO and Wokwi extensions.
2. Create `include/secrets.h` with `#define DEVICE_KEY ""`.
3. In `src/main.cpp`, set `API_URL` to your server and `MQTT_NAME` to your broker name.
4. Build, then start the Wokwi simulator. Copy the MAC from the Serial Monitor.
5. `POST /fans` with `{"mac": "<MAC>", "room": "bathroom"}`, put the returned `key` in `include/secrets.h`, build and start again.
6. `PUT /fans/<MAC>/settings` with `{"mode": "auto", "max_humidity": 65}`, then drag the DHT22's humidity above 65: the fan LED turns on.