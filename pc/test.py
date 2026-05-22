import json
import time

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import ASYNCHRONOUS

influx_bucket = "magenta-mimas"
influx_org = "Magenta-Mimas"
influx_token = "Lu4DEFL8LTYn_gDsVFaxQcatzz85YNx7nPHZXNwbkrpFKWaGhOjdVIOy6TkAxKAPmo_0UG6KuiJCapZsN1IPvA=="
influx_url = "https://us-east-1-1.aws.cloud2.influxdata.com/"
influx_client = InfluxDBClient(url=influx_url, token=influx_token, org=influx_org)
write_api = influx_client.write_api(write_options=ASYNCHRONOUS)

enabled = False

def write_to_db(line: str):
    data = json.loads(line)
    if isinstance(data, dict) and enabled:
        node_a = data["nodeA"]
        node_b = data["nodeB"]
        velocity = data["speed"] * data["direction"]

        node_a_p = (Point("nodes")
                    .tag("location", "node_a")
                    .field("conn", node_a["connection_status"])
                    .field("chg", node_a["charge"])
                    .field("mv", node_a["mv"]))
        node_b_p = (Point("nodes")
                    .tag("location", "node_b")
                    .field("conn", node_b["connection_status"])
                    .field("chg", node_b["charge"])
                    .field("mv", node_b["mv"]))
        helm_p = (Point("helm")
                  .tag("location", "helm")
                  .field("velocity", velocity))
        records = [node_a_p, node_b_p, helm_p]

        write_api.write(
                bucket=influx_bucket, 
                org=influx_org, 
                record=records, 
                write_precision=WritePrecision.S
        )

if __name__ == "__main__":
    with open('example.txt', 'r') as file:
        for line in file:
            line = line.strip()
            write_to_db(line)
            time.sleep(3)
