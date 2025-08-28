# version 450

// Input images
layout (input_attachment_index = 0, binding = 0) uniform subpassInput cmykInput;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput spotInput;

// Textures
layout(binding = 2) uniform sampler2D fooLut;
layout(binding = 3) uniform sampler2D barLut;

// Output
layout (location = 0) out vec4 resultCmyk;

// Push constants - not used
layout (push_constant) uniform PushConstants
{
    int   tileX;
    int   tileY;
} pushConstants;

void main()
{
    // Read the current process components
    vec4 cmyk = subpassLoad(cmykInput);

    // Read the spot values
    vec4 spotValues = subpassLoad(spotInput);

    // Initial result
    resultCmyk = cmyk;

    float tint;
    int lutIndex;
    vec4 lutVal;
    
    //
    // Merge in "Foo"
    //
    
    // The tint is (remember that colours are inverted with respect to natural here):
    tint = 1.0 - spotValues.r;
    
    // Next, pull the colour from the LUT. We'll use texelFetch() here for low level
    // control of pulling from the LUT. We could also perform interpolation here, but
    // for this example we have a 256 entry lut, and we're rendering 8 bit. So this is
    // enough.
    lutIndex = int(round(tint * 255.0));
    lutVal = texelFetch(fooLut, ivec2(lutIndex, 0), 0);
    
    // Merge into the process. Remember, everything is inverted.
    resultCmyk *= (1.0 - lutVal);
    
    //
    // Similarly, merge in "Bar"
    //
    
    tint = 1.0 - spotValues.g;
    lutIndex = int(round(tint * 255.0));
    lutVal = texelFetch(barLut, ivec2(lutIndex, 0), 0);
    resultCmyk *= (1.0 - lutVal);

    // Done!
}

