struct ButterflyInstance
{
    float3 position;
    float3 direction;
    float offset;
    float textureIndex;
};
float rand(float2 n)
{
    float f;
    return fract(sin(dot(n.xy, (float2)(12.9898f, 78.233f)))* 43758.5453f, &f);
}

//algorithm http://wonderfl.net/c/zyEA
kernel void butterfly_main(global struct ButterflyInstance *instance, global float3 *velocity,
                           float trakingX, float trakingY, float trakingZ,
                           float time)
{
    int index = get_global_id(0);
    
    instance[index].position += velocity[index];
    instance[index].direction = velocity[index];
    
    //tracking
    float3 traking = (float3)(trakingX, trakingY, trakingZ);
    float dist = fast_distance(traking, instance[index].position);
    dist = max(dist, 1.0f);
    
    //velocity update
    velocity[index] -= fast_normalize(instance[index].position - traking) * 2.0f / dist * 0.5f;
    
    //random 
    float arg2 = time + instance[index].offset;
    float rx = rand((float2)(instance[index].position.x, arg2));
    float ry = rand((float2)(instance[index].position.y, arg2));
    float rz = rand((float2)(instance[index].position.z, arg2));
    
    float3 r = (float3)(rx, ry, rz);
    
    velocity[index] += ((r - 0.5f) * 2.0f) * 0.05f;
    
    velocity[index] += -velocity[index] * 0.03f;
}
