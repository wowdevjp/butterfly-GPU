#version 120

//uniform sampler2D u_image;
uniform sampler2D u_images[8];

varying vec2 v_uv;
varying float v_textureIndex;

void main()
{
    vec4 color = texture2D(u_images[int(v_textureIndex)], v_uv);
    if(color.a < 0.5)
        discard;
    
	gl_FragColor = color;
}



