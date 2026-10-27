const bytes = @embedFile("assets/bundle.json");
export fn kafel_weather_assets_data() [*]const u8 { return bytes.ptr; }
export fn kafel_weather_assets_size() usize { return bytes.len; }
