from pydantic import BaseModel

class SensorPayload(BaseModel):
    roll:       float
    pitch:      float
    gx:         float
    gy:         float
    gz:         float
    accelX:     float
    accelY:     float
    accelZ:     float
    accelEvent: str
    lat:        float
    lon:        float
    alt:        float
    speed:      float
