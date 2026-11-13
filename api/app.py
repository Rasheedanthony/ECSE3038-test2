# ECSE3038 Test 2: the bathroom fan API.
# Fans, readings and settings live in MongoDB Atlas. PUT settings are pushed
# to the board over MQTT, retained, on ecse3038/<MQTT_NAME>/fan/settings.

import json
import os
import secrets
from datetime import datetime, timezone
from typing import Literal

from dotenv import load_dotenv
from fastapi import FastAPI, Header, HTTPException, Query, Response
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from pymongo import ASCENDING, MongoClient
from pymongo.errors import DuplicateKeyError
import paho.mqtt.publish as publish

load_dotenv()
client = MongoClient(os.getenv("MONGODB_URI"), tz_aware=True)
db = client["test2"]
fans = db["fans"]
readings = db["readings"]
settings = db["settings"]

fans.create_index([("mac", ASCENDING)], unique=True)
fans.create_index([("key", ASCENDING)], unique=True)

MQTT_BROKER = "135.232.203.144"
MQTT_NAME = os.getenv("MQTT_NAME", "yourname")

app = FastAPI(title="ECSE3038 Test 2: bathroom fan")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class FanIn(BaseModel):
    mac: str
    room: str


class ReadingIn(BaseModel):
    mac: str
    humidity: float


class SettingsIn(BaseModel):
    mode: Literal["auto", "on", "off"]
    max_humidity: float = Field(ge=0, le=100)


def fan_for_key(key: str | None):
    """The fan that owns this key, or 401 when there is no key or it was never issued."""
    if not key:
        raise HTTPException(status_code=401, detail="Missing X-API-Key")
    fan = fans.find_one({"key": key})
    if fan is None:
        raise HTTPException(status_code=401, detail="Unknown API key")
    return fan


# ---------- A1: fans ----------

@app.post("/fans", status_code=201)
def create_fan(fan: FanIn):
    doc = {"mac": fan.mac, "room": fan.room, "key": secrets.token_urlsafe(16)}
    try:
        fans.insert_one(doc)
    except DuplicateKeyError:
        raise HTTPException(status_code=409, detail="A fan with MAC " + fan.mac + " already exists")
    doc.pop("_id", None)
    return doc


@app.get("/fans")
def list_fans():
    return list(fans.find({}, {"_id": 0, "key": 0}))


@app.get("/fans/{mac}")
def get_fan(mac: str):
    fan = fans.find_one({"mac": mac}, {"_id": 0, "key": 0})
    if fan is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)
    return fan


# ---------- A2 and A3: readings ----------

@app.post("/readings", status_code=201)
def create_reading(reading: ReadingIn, x_api_key: str | None = Header(default=None)):
    fan = fan_for_key(x_api_key)                          # 401
    if fan["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail="This key belongs to another fan")
    if reading.humidity < 0 or reading.humidity > 100:
        raise HTTPException(status_code=422, detail="humidity must be from 0 to 100")
    doc = {"mac": reading.mac, "humidity": reading.humidity, "time": datetime.now(timezone.utc)}
    readings.insert_one(doc)
    doc.pop("_id", None)
    return doc


@app.get("/readings")
def list_readings(mac: str | None = None, limit: int = Query(default=10, ge=1, le=100)):
    query = {}
    if mac is not None:
        query["mac"] = mac
    return list(readings.find(query, {"_id": 0}).sort("time", -1).limit(limit))


# ---------- A4: settings ----------

@app.put("/fans/{mac}/settings")
def put_settings(mac: str, new: SettingsIn, response: Response):
    if fans.find_one({"mac": mac}) is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)

    doc = {"mac": mac, "mode": new.mode, "max_humidity": float(new.max_humidity)}
    result = settings.replace_one({"mac": mac}, doc, upsert=True)
    if result.upserted_id is not None:
        response.status_code = 201
    doc.pop("_id", None)

    payload = json.dumps({"mode": new.mode, "max_humidity": float(new.max_humidity)})
    try:
        publish.single("ecse3038/" + MQTT_NAME + "/fan/settings", payload,
                       retain=True, hostname=MQTT_BROKER)
    except Exception as error:
        print("Could not publish the settings:", error)
    return doc


# ---------- A5: delete ----------

@app.delete("/fans/{mac}", status_code=204)
def delete_fan(mac: str, x_api_key: str | None = Header(default=None)):
    if fans.find_one({"mac": mac}) is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)
    owner = fan_for_key(x_api_key)                        # 401
    if owner["mac"] != mac:
        raise HTTPException(status_code=403, detail="This key belongs to another fan")
    fans.delete_one({"mac": mac})
    settings.delete_one({"mac": mac})
    return Response(status_code=204)