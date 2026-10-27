const regular = @embedFile("assets/ibm-plex-sans/IBMPlexSans-Regular.ttf");
const bold = @embedFile("assets/ibm-plex-sans/IBMPlexSans-Bold.ttf");
export fn ui_sans_regular_data() [*]const u8 { return regular.ptr; }
export fn ui_sans_regular_size() usize { return regular.len; }
export fn ui_sans_bold_data() [*]const u8 { return bold.ptr; }
export fn ui_sans_bold_size() usize { return bold.len; }
