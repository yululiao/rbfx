// Loading screen configuration. Every field has a working default so the boot screen is
// usable before a project supplies its own art; the packaging pipeline may overwrite this
// file per project. Distances are authored for reference_resolution_long_edge and scale
// with the actual canvas size.

rbfx.loading_config = {
    // Scale reference for the progress bar height and the font.
    reference_resolution_long_edge: 1920,
    process_bar_height: 14,
    font_size: 28,

    // Progress bar geometry, relative to the canvas.
    process_bar_top_rate: 0.78,   // distance from the top, as a fraction of canvas height
    process_bar_width_rate: 0.6,  // bar length as a fraction of canvas width

    // Colors.
    text_style: "#ffffff",
    process_bar_background_left_color: "#3a3a3a",
    process_bar_background_right_color: "#3a3a3a",
    process_bar_current_left_color: "#4fa3ff",
    process_bar_current_right_color: "#4fa3ff",
    background_style_when_no_image: "#232323",

    // Optional full-screen artwork, package-relative. An empty string disables the image and
    // the solid-color background is shown instead.
    image: "",

    // Optional startup splash, shown for splash_time seconds before the progress screen.
    // Download and compile progress keeps running while the splash is on screen.
    splash: "",
    splash_time: 0
};
