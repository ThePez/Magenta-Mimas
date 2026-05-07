const unfiltered = {
  dot: document.getElementById("u_dot"),
  text: document.getElementById("u_pos")
}

const kalman = {
  dot: document.getElementById("k_dot"),
  text: document.getElementById("k_pos")
}

// Beacon Locations [pixels]
const beaconLocations = {
  "4011-A": { x: 0 - 35,   y: 340      },
  "4011-B": { x: 0 - 35,   y: 170 - 15 },
  "4011-C": { x: 0 - 35,   y: 0 - 35   },
  "4011-D": { x: 225 - 15, y: 0 - 35   },
  "4011-E": { x: 450 - 15, y: 0 - 35   },
  "4011-F": { x: 675 - 15, y: 0 - 35   },
  "4011-G": { x: 900,      y: 0 - 35   },
  "4011-H": { x: 900,      y: 170 - 15 },
  "4011-I": { x: 900,      y: 340      },
  "4011-J": { x: 675 - 15, y: 340      },
  "4011-K": { x: 450 - 15, y: 340      },
  "4011-L": { x: 225 - 15, y: 340      },
  "4011-M": { x: 450 - 15, y: 170 - 15 }
} 

const lengthX = 340;
const lengthY = 900;

function updatePosition(data, item) {
  console.log(data.x, data.y);
  item.dot.style.left = `${data.y + lengthY/2 - 5}px`;
  item.dot.style.top = `${data.x + lengthX/2 - 5}px`;
  item.text.textContent = `X: ${data.x}cm, Y: ${data.y}cm @ [${data.time}]`;
}

function updateLog(data) {
  if (data.length == 0) {
    return;
  }

  const logbox = document.getElementById("logbox");

  logbox.value = localStorage.getItem('logs');
  for (const log of data) {
    logbox.value += `[${log.time}]: ${log.log}\n`; 
  }
  localStorage.setItem('logs', logbox.value);
}

function toggleAll() {
  document.querySelectorAll('input[name="enabled"]').forEach(cb => {
    cb.click();
  });
}

function renderBeacons() {
  const container = document.getElementById("grid");
  document.querySelectorAll(".beacon").forEach(el => el.remove());

  const enabledBeacons = window.enabledBeacons || [];

  enabledBeacons.forEach(id => {
    const beacon = beaconLocations[id];
    const wrapper = document.createElement("div");
    wrapper.className = "beacon";

    wrapper.style.position = "absolute";
    wrapper.style.left = beacon.x + "px";
    wrapper.style.top = beacon.y + "px";

    wrapper.style.display = "flex";
    wrapper.style.flexDirection = "column";
    wrapper.style.alignItems = "center";

    const img = document.createElement("img");
    img.src = "/static/img/beacon.png";
    img.style.width = "25px";
    img.style.height = "25px";

    const text = document.createElement("div");
    text.innerText = id;
    text.style.fontSize = "10px";
    text.style.whiteSpace = "nowrap";

    wrapper.appendChild(img);
    wrapper.appendChild(text);
    container.appendChild(wrapper);
  });
}

async function get_position() {
  const res = await fetch("/get_position");
  const data = await res.json();
  updatePosition(data[0], unfiltered);
  updatePosition(data[1], kalman);
  updateLog(data[2]);
}

setInterval(get_position, 100);
document.getElementById("logbox").value = localStorage.getItem('logs');
renderBeacons();
