const params = new URLSearchParams(window.location.search);
const alertValue = params.get("alert");

if (alertValue) {
  alert(alertValue);
} 