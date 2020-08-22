uniform sampler2D colorMap;
uniform sampler1D colorPalette;

void main()
{
    //gl_FragColor = vec4(texture2D(colorMap, gl_TexCoord[0].xy).r,0,0,1);
    //gl_FragColor = vec4(gl_TexCoord[0].xy, 0.0, 1.0);
    float colorIndex = texture2D(colorMap, gl_TexCoord[0].xy).r;
    gl_FragColor = vec4(texture1D(colorPalette, colorIndex).xyz,1);
    //gl_FragColor = vec4(texture2D(colorMap, gl_TexCoord[0].xy).r,0,0,1);
    //gl_FragColor = vec4(texture2D(colorPalette, gl_TexCoord[0].xy).rgb,1);
}
