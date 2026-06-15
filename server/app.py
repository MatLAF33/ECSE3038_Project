from fastapi import FastAPI, HTTPException, status
from fastapi.responses import JSONResponse
from motor.motor_asyncio import AsyncIOMotorClient
from pydantic import BaseModel
from dotenv import dotenv_values
from datetime import datetime, timedelta
from fastapi.middleware.cors import CORSMiddleware
import os

config = dotenv_values(".env")

#MONGO_URI = config["MONGO_URI"]
MONGO_URI = os.getenv("MONGO_URI") or config["MONGO_URI"] #for render 
client = AsyncIOMotorClient(MONGO_URI)
db = client["IoT_Project_db"]

app = FastAPI()
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # allow frontend to access backend
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

#endpoints:
#PUT  /settings
#POST /data
#GET  /state
#GET  /graph?size=10



#PUT  /settings with db
# json
# {
#     "user_temp": 28,
#     "user_light": "18:00:00",
#     "light_duration": "4h"
# }



class UserSettings(BaseModel):
    user_temp: float # fan turn on threshold may add some hysteresis later
    user_light: str #turn on time
    light_duration: str # on duration server handle that

def parse_duration(duration):
    duration = duration.lower().strip() #put verything in lower case AND REMOVE SPACE 

    hours = 0
    minutes = 0

    h_index = duration.find("h")
    m_index = duration.find("m")

    if h_index != -1:
        hours_text = duration[0:h_index]
        hours = int(hours_text)

        if m_index != -1:
            minutes_text = duration[h_index + 1:m_index] #Pull char after h 
            minutes = int(minutes_text)

    #take min if there no h
    elif m_index != -1:
        minutes_text = duration[0:m_index]
        minutes = int(minutes_text)

    else:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Invalid duration format required format ex: 1h30m or 30m"
        )

    return timedelta(hours=hours, minutes=minutes) 

@app.put("/settings")
async def update_settings(settings: UserSettings):

    # Convert user_light string into Python time
    light_on_time = datetime.strptime(settings.user_light, "%H:%M:%S")

    # Convert light_duration string into a time duration
    duration = parse_duration(settings.light_duration)

    # calculating time to turn off light by adding timedelta duration to light on time
    light_off_time = light_on_time + duration

    # Convert off time back into HH:MM:SS string
    light_time_off = light_off_time.strftime("%H:%M:%S")

    # Document to save in MongoDB
    # doc = {
    #     "user_temp": settings.user_temp,
    #     "user_light": settings.user_light,
    #     "light_time_off": light_time_off
    # }

    # # Only one settings document should exist
    # await db["settings"].delete_many({})
    # await db["settings"].insert_one(doc)

    doc = settings.model_dump()

    doc["light_time_off"] = light_time_off

    # remove light_duration from doc only using light_off
    doc.pop("light_duration")

    # #delete old from db and replace
    # await db["settings"].delete_many({})

    # await db["settings"].insert_one(doc)

    await db["settings"].update_one({},{"$set": doc},upsert=True) # update if exist and create if dones't (upsert=true )

    updated_doc = await db["settings"].find_one({}, {"_id": 0}) #remove mongdb id from response

    return updated_doc

#test DB
@app.get("/settings")
async def get_settings():
    doc = await db["settings"].find_one({}, {"_id": 0})

    if not doc:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="No settings found"
        )

    return doc

#POST /data with db
#esp32 sent data
class SensorData(BaseModel):
    temperature: float
    presence: bool


@app.post("/data", status_code=status.HTTP_201_CREATED)
async def create_sensor_data(sensor_data: SensorData):

    doc = sensor_data.model_dump()

    #add timestamp to doc before saving to db
    doc["datetime"] = datetime.now().isoformat(timespec="seconds") #TIMESTAMP WITH EXLCUDING MICROSEONDS (TIMESPEC=SECONDS)

    #sensor data in db
    await db["sensor_data"].insert_one(doc)

    saved_doc = await db["sensor_data"].find_one({"datetime": doc["datetime"]},{"_id": 0}
    )

    return saved_doc

#GET  /state AND LOGIC FOR FAN AND LIGHT CONTROL
@app.get("/state")
async def get_state():

    # Get latest settings from database
    settings = await db["settings"].find_one({}, {"_id": 0})

    if not settings:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND,detail="No settings found")
    
    # Get latest sensor reading from db
    sensor_data = await db["sensor_data"].find_one({},{"_id": 0}, sort=[("datetime", -1)] # sort=[("datetime", -1) newest datetime first
    )

    if not sensor_data:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND,detail="No sensor data found")

    # Fan logic
    fan_state = False
    #add hysteresis 
    if sensor_data["temperature"] > settings["user_temp"] and sensor_data["presence"] == True:
        fan_state = True

    # Light logic
    light_state = False

    current_time = datetime.now().strftime("%H:%M:%S")

    if current_time >= settings["user_light"] and current_time <= settings["light_time_off"] and sensor_data["presence"] == True:
        light_state = True

    #send response with fan and light state to esp32
    response = {
        "fan": fan_state,
        "light": light_state
    }

    return JSONResponse(content=response, status_code=status.HTTP_200_OK)

       
@app.get("/graph")
async def get_graph_data(size: int = 10): #default size is 10 if no query parameter given

    readings = []

    # Get newest readings first
    async for doc in db["sensor_data"].find({}, {"_id": 0}).sort("datetime", -1).limit(size):
        readings.append(doc)

    # Reverse so graph shows oldest to newest
    readings.reverse()

    return readings
