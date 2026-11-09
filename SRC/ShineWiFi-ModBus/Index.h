#pragma once

const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html lang="en">

<!-- Rui Santos - Complete project details at https://RandomNerdTutorials.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files.
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software. -->

<head>
    <meta charset='utf-8'>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Growatt Inverter</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.2.1/chart.umd.min.js" integrity="sha512-GCiwmzA0bNGVsp1otzTJ4LWQT2jjGJENLGyLlerlzckNI30moi2EQT0AfRI7fLYYYDKR+7hnuh35r3y1uJzugw==" crossorigin="anonymous" referrerpolicy="no-referrer"></script>

    <style>
        body {
            min-width: 310px;
            max-width: 800px;
            height: 400px;
            margin: 0 auto;
            font-family: Arial;
        }

        h2 {
            font-size: 2.5rem;
            text-align: center;
        }

        div {
            font-size: 1rem;
            padding: 10px 0px 10px 0px;
        }

        .linkButtonBar {
            text-align: center;
            padding: 20px 0px 20px 0px;
        }

        .linkButton {
            -webkit-border-radius: 4px;
            -moz-border-radius: 4px;
            border-radius: 4px;
            border: solid 1px #91ca5f;
            text-shadow: 0 -1px 0 rgba(0, 0, 0, 0.4);
            -webkit-box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            -moz-box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            background: #6eb92b;
            color: #FFF;
            padding: 8px 12px;
            text-decoration: none;
            display: inline-block;
            margin: 2px;
            font-size: .8rem;
            text-align: center;
        }

        .yellow {
            border: solid 1px rgb(202, 189, 95);
            text-shadow: 0 -1px 0 rgba(0, 0, 0, 0.4);
            -webkit-box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            -moz-box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            background:rgb(185, 178, 43);
        }

        .red {
            border: solid 1px rgb(202, 95, 95);
            text-shadow: 0 -1px 0 rgba(0, 0, 0, 0.4);
            -webkit-box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            -moz-box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.4), 0 1px 1px rgba(0, 0, 0, 0.2);
            background:rgb(185, 43, 43);
        }

        .doughnutRow {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-around;
        }

        .doughnutBox {
            width: 45%;
            min-width: 150px;
        }

        .infoLine {
            font-size: .8rem;
            color: #666;
            text-align: center;
        }
    </style>
</head>

<body>
    <h2>Growatt Inverter</h2>

    <div><canvas id="powerChart"></canvas></div>

    <div class="doughnutRow">
        <div class="doughnutBox"><canvas id="batteryState"></canvas></div>
        <div class="doughnutBox"><canvas id="powerUsage"></canvas></div>
    </div>

    <div class="linkButtonBar">
        <a href="./status" class="linkButton">Json</a>
        <a href="./uiStatus" class="linkButton">UI Json</a>
        <a href="./metrics" class="linkButton">Metrics</a>
        <a href="./modbusTcp" class="linkButton">Modbus TCP</a>
        <a href="./debug" class="linkButton">Log</a>
        <a onClick="return confirm('Starting config AP will disconnect you from the device. Are you sure?');" href="./startAp" class="linkButton yellow">Start Config AP</a>
        <a onClick="return confirm('This will reboot the Wifi Stick. Are you sure?');" href="./reboot" class="linkButton yellow">Reboot</a>
        <a href="./postCommunicationModbus" class="linkButton red">RW Modbus</a>
    </div>

    <div id="ModbusTcpInfo" class="infoLine"></div>

    <div id="DataContainer"></div>

<script>
    let initialised = false;
    const POLL_INTERVAL = 5000;
    // keep the line chart from growing forever (5s * 720 = 1h)
    const MAX_POINTS = 720;
    // these values are plotted on the right axis (0..100)
    const RIGHT_AXIS_KEYS = ['SOC', 'BatteryTemperature', 'InverterTemperature'];

    let useGrid = 0;
    let useBat = 0;
    let localLoad = 0;

    const CHART_COLORS = {
        red: 'rgb(255, 99, 132)',
        orange: 'rgb(255, 159, 64)',
        yellow: 'rgb(255, 205, 86)',
        green: 'rgb(75, 192, 192)',
        blue: 'rgb(54, 162, 235)',
        purple: 'rgb(153, 102, 255)',
        grey: 'rgb(201, 203, 207)'
    };

    const NAMED_COLORS = [
        CHART_COLORS.red,
        CHART_COLORS.orange,
        CHART_COLORS.yellow,
        CHART_COLORS.green,
        CHART_COLORS.blue,
        CHART_COLORS.purple,
        CHART_COLORS.grey,
    ];

    function namedColor(index) {
        return NAMED_COLORS[index % NAMED_COLORS.length];
    }

    const powerchartData = {
        labels: [],
        datasets: [],
    };

    const batData = {
        labels: ['SOC', 'empty'],
        datasets: [{
            label: 'SOC',
            data: [0, 100],
            backgroundColor: [CHART_COLORS.grey, CHART_COLORS.grey],
        }],
    };

    const usageData = {
        labels: ['grid', 'battery', 'pv direct'],
        datasets: [{
            label: 'kWh today',
            data: [0, 0, 0],
            backgroundColor: [CHART_COLORS.red, CHART_COLORS.green, CHART_COLORS.orange],
        }],
    };

    let powerchart = new Chart(document.getElementById('powerChart'), {
        type: 'line',
        data: powerchartData,
        options: {
            animation: false,
            scales: {
                y: {
                    beginAtZero: true
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    min: 0.0,
                    max: 100.0,
                    grid: { drawOnChartArea: false }
                }
            }
        }
    });

    let batchart = new Chart(document.getElementById('batteryState'), {
        type: 'doughnut',
        data: batData,
        options: {
            responsive: true,
            plugins: {
                legend: { position: 'top' },
                title: { display: true, text: 'Battery state' }
            }
        },
    });

    let usagechart = new Chart(document.getElementById('powerUsage'), {
        type: 'doughnut',
        data: usageData,
        options: {
            responsive: true,
            plugins: {
                legend: { position: 'top' },
                title: { display: true, text: 'Power usage today' }
            }
        },
    });

    function fillBatteryState(soc) {
        let color;
        if (soc < 30) {
            color = CHART_COLORS.red;
        } else if (soc < 50) {
            color = CHART_COLORS.orange;
        } else if (soc <= 75) {
            color = CHART_COLORS.yellow;
        } else {
            color = CHART_COLORS.green;
        }
        batData.datasets[0].backgroundColor[0] = color;
        batData.datasets[0].data[0] = soc;
        batData.datasets[0].data[1] = 100.0 - soc;
        batchart.update();
    }

    function fillUsage(grid, battery, load) {
        let pvDirect = load - (grid + battery);
        if (pvDirect < 0) {
            pvDirect = 0;
        }
        usageData.datasets[0].data[0] = grid;
        usageData.datasets[0].data[1] = battery;
        usageData.datasets[0].data[2] = Math.round(pvDirect * 10) / 10;
        usagechart.update();
    }

    function valueHtml(key, entry) {
        return "<a href=\"./value/" + key + "\">" + key + "</a>: " +
               entry[0] + "&#8239;" + entry[1];
    }

    function processData(obj) {
        let date = new Date();
        powerchartData.labels.push(date.toLocaleTimeString());
        if (powerchartData.labels.length > MAX_POINTS) {
            powerchartData.labels.shift();
            for (let dset of powerchartData.datasets) {
                dset.data.shift();
            }
        }

        let container = document.getElementById("DataContainer");
        if (initialised == false) {
            initialised = true;
            container.innerHTML = "";
        }

        for (let key in obj) {
            let entry = obj[key];

            // line chart
            if (entry[2] == true) {
                let dset = powerchartData.datasets.find(d => d.label == key);
                if (dset === undefined) {
                    dset = {
                        label: key,
                        data: [],
                        fill: false,
                        borderColor: namedColor(powerchartData.datasets.length),
                        tension: 0.1,
                        pointRadius: 0,
                        yAxisID: RIGHT_AXIS_KEYS.includes(key) ? 'y1' : 'y',
                    };
                    // pad so that all datasets line up with the labels
                    while (dset.data.length < powerchartData.labels.length - 1) {
                        dset.data.push(null);
                    }
                    powerchartData.datasets.push(dset);
                }
                dset.data.push(entry[0]);
            }

            // data view
            let element = document.getElementById(key);
            if (element === null) {
                element = document.createElement("p");
                element.setAttribute("id", key);
                container.appendChild(element);
            }
            element.innerHTML = valueHtml(key, entry);

            // doughnuts
            if (key == "SOC") {
                fillBatteryState(entry[0]);
            } else if (key == "EnergyToUserToday") {
                useGrid = entry[0];
            } else if (key == "DischargeEnergyToday") {
                useBat = entry[0];
            } else if (key == "LocalLoadEnergyToday") {
                localLoad = entry[0];
            }
        }
        fillUsage(useGrid, useBat, localLoad);
        powerchart.update();
    }

    function poll() {
        var xhttp = new XMLHttpRequest();
        xhttp.onreadystatechange = function () {
            if (this.readyState == 4 && this.status == 200) {
                processData(JSON.parse(this.responseText));
            }
        }
        xhttp.open("GET", "./uiStatus", true);
        xhttp.send();
    }

    function pollModbusTcp() {
        var xhttp = new XMLHttpRequest();
        xhttp.onreadystatechange = function () {
            if (this.readyState != 4) {
                return;
            }
            let info = document.getElementById("ModbusTcpInfo");
            if (this.status != 200) {
                info.innerHTML = "";
                return;
            }
            let s = JSON.parse(this.responseText);
            info.innerHTML = "Modbus TCP port " + s.port +
                (s.write_enabled ? " (read/write)" : " (read only)") +
                " &middot; clients: " + s.clients +
                " &middot; requests: " + s.requests +
                " &middot; errors: " + s.errors;
        }
        xhttp.open("GET", "./modbusTcp", true);
        xhttp.send();
    }

    // fetch immediately instead of waiting for the first interval
    poll();
    pollModbusTcp();
    setInterval(poll, POLL_INTERVAL);
    setInterval(pollModbusTcp, 30000);
</script>
</body>
</html>
)=====";

const char SendPostSite_page[] PROGMEM = R"=====(
<!DOCTYPE HTML><html>
<!-- Rui Santos - Complete project details at https://RandomNerdTutorials.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files.
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software. -->
<head>
  <meta charset='utf-8'>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Growatt Inverter</title>
</head>
<body>
  <h2>Growatt Post Communication Modbus</h2>
  <form action="/postCommunicationModbus_p" method="POST">
    <input type="text" name="reg" placeholder="Register ID"></br>
    <input type="text" name="val" placeholder="Input Value (16bit only!)"></br>
    <select name="type">
      <option value="16b" selected>16b</option>
      <option value="32b">32b</option>
    </select></br>
    <select name="operation">
      <option value="R" selected>Read</option>
      <option value="W">Write</option>
    </select></br>
    <select name="registerType">
      <option value="I" selected>Input Register</option>
      <option value="H">Holding Register</option>
    </select></br>
    <input type="submit" value="go">
  </form>
  <a href=".">back</a>
</body>
</html>
)=====";
