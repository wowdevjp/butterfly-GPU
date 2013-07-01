#version 120

uniform mat4 u_transform;
uniform float u_time;
uniform float u_size;

attribute vec3 a_position0;
attribute vec3 a_position1;
attribute vec2 a_texcoord;

/*instance*/
attribute vec3 ai_position;
attribute vec3 ai_direction;
attribute float ai_offset;
attribute float ai_textureIndex;

varying vec2 v_uv;
varying float v_textureIndex;

void main()
{
    //向きベクトルai_directionに向くように回転させる
    //基底の変換、というらしい
    vec3 up_global = vec3(0.0, 1.0, 0.0);
    
    vec3 zaxis = normalize(ai_direction);
    vec3 xaxis = normalize(cross(ai_direction, up_global));
    vec3 yaxis = normalize(cross(xaxis, zaxis));
    
    mat3 directionRotate = mat3(xaxis.x, xaxis.y, xaxis.z,
                                yaxis.x, yaxis.y, yaxis.z,
                                zaxis.x, zaxis.y, zaxis.z);

    float s = sin(u_time * 30.0 + ai_offset) * 0.5 + 0.5;
    vec3 position = ai_position + directionRotate * mix(a_position0, a_position1, s) * u_size;
	gl_Position = u_transform * vec4(position, 1.0);
    v_uv = a_texcoord;
    v_textureIndex = ai_textureIndex;
}
