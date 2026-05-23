var Clay = require('pebble-clay');

var clayConfig = [
  { "type": "heading", "defaultValue": "Watchface Settings" },
  { 
    "type": "section", 
    "items": [
      { "type": "heading", "defaultValue": "Weather API" },
      { "type": "input", "messageKey": "API_KEY", "label": "OpenWeatherMap API Key" },
      { 
        "type": "select", 
        "messageKey": "UNITS", 
        "label": "Distance & Temp Units", 
        "defaultValue": "imperial", 
        "options": [
          { "label": "Fahrenheit & Miles", "value": "imperial" }, 
          { "label": "Celsius & Kilometers", "value": "metric" }
        ]
      },
      { 
        "type": "slider", 
        "messageKey": "UPDATE_FREQ", 
        "label": "Weather Update Frequency (Minutes)",
        "description": "Recommended: 30 minutes.", 
        "defaultValue": 30, "min": 10, "max": 120, "step": 5 
      },
      { "type": "heading", "defaultValue": "System" },
      { 
        "type": "select", 
        "messageKey": "TIME_FORMAT", 
        "label": "Time Format", 
        "defaultValue": "12h", 
        "options": [
          { "label": "12 Hour", "value": "12h" }, 
          { "label": "24 Hour", "value": "24h" }
        ]
      },
      { "type": "heading", "defaultValue": "Health Goals" },
      { 
        "type": "slider", 
        "messageKey": "STEP_GOAL", 
        "label": "Daily Step Goal", 
        "defaultValue": 10000, "min": 2000, "max": 20000, "step": 1000 
      },
      { 
        "type": "slider", 
        "messageKey": "ACTIVE_GOAL", 
        "label": "Active Minutes Goal", 
        "defaultValue": 30, "min": 10, "max": 120, "step": 5 
      }
    ]
  },
  { "type": "submit", "defaultValue": "Save Settings" }
];

var clay = new Clay(clayConfig);

function fetchWeather(pos) {
  var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
  if (!settings.API_KEY) return;
  
  var units = settings.UNITS || 'imperial';
  var isMetric = (units === 'metric') ? 1 : 0;
  
  // Implemented the OneCall 3.0 endpoint from your weather app
  var url = 'https://api.openweathermap.org/data/3.0/onecall?lat=' + pos.coords.latitude + '&lon=' + pos.coords.longitude + '&exclude=minutely,hourly,alerts&units=' + units + '&appid=' + settings.API_KEY;

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (this.status === 200) {
      var json = JSON.parse(this.responseText);
      
      var isNight = json.current.weather[0].icon.includes('n') ? 1 : 0;
      var uvLevel = Math.round(json.current.uvi);
      var dayMax = Math.round(json.daily[0].temp.max);
      var dayMin = Math.round(json.daily[0].temp.min);
      var currentTemp = Math.round(json.current.temp);

      var dict = {
        'TEMP_CURRENT': currentTemp,
        'CONDITIONS': json.current.weather[0].icon,
        'TEMP_HIGH': dayMax,
        'TEMP_LOW': dayMin,
        'UNITS': isMetric,
        'STEP_GOAL': parseInt(settings.STEP_GOAL) || 10000,
        'ACTIVE_GOAL': parseInt(settings.ACTIVE_GOAL) || 30,
        'IS_NIGHT': isNight,
        'UPDATE_FREQ': parseInt(settings.UPDATE_FREQ) || 30,
        'UV_INDEX': uvLevel
      };
      
      Pebble.sendAppMessage(dict);
    }
  };
  xhr.open('GET', url);
  xhr.send();
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(fetchWeather, function(){}, {timeout: 15000, maximumAge: 60000});
}

Pebble.addEventListener('ready', function() { getWeather(); });
Pebble.addEventListener('appmessage', function(e) { getWeather(); });

Pebble.addEventListener('webviewclosed', function() {
  var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
  var dict = {
    'TIME_FORMAT': settings.TIME_FORMAT === '24h' ? 1 : 0,
    'UNITS': settings.UNITS === 'metric' ? 1 : 0,
    'STEP_GOAL': parseInt(settings.STEP_GOAL) || 10000,
    'ACTIVE_GOAL': parseInt(settings.ACTIVE_GOAL) || 30,
    'UPDATE_FREQ': parseInt(settings.UPDATE_FREQ) || 30
  };
  Pebble.sendAppMessage(dict, function() { getWeather(); });
});
