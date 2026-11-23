#pragma once

// Return a district index only when the entire accuracy circle is inside the district.
int districtAt(double latitude, double longitude, double accuracyMetres);
int locateUsingWifi();
