/*
 *  GPUStress
 *  Copyright (C) 2014 Mateusz Szpakowski
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma OPENCL FP_CONTRACT OFF

static inline float4 polyeval4d(float p0, float p1, float p2, float p3, float p4, float4 x)
{
    return mad(x, mad(x, mad(x, mad(x, p4, p3), p2), p1), p0);
}

kernel void gpuStress(uint n, const global float4* restrict input,
            global float4* restrict output, float p0, float p1, float p2, float p3, float p4)
{
    size_t gid = get_global_id(0);
    
    for (uint i = 0; i < BLOCKSNUM; i++)
    {
        float4 x1 = input[gid*4];
        float4 x2 = input[gid*4+1];
        float4 x3 = input[gid*4+2];
        float4 x4 = input[gid*4+3];
        for (uint j = 0; j < KITERSNUM; j++)
        {
            x1 = polyeval4d(p0, p1, p2, p3, p4, x1);
            x2 = polyeval4d(p0, p1, p2, p3, p4, x2);
            x3 = polyeval4d(p0, p1, p2, p3, p4, x3);
            x4 = polyeval4d(p0, p1, p2, p3, p4, x4);
        }
        
        output[gid*4] = x1;
        output[gid*4+1] = x2;
        output[gid*4+2] = x3;
        output[gid*4+3] = x4;
        
        gid += get_global_size(0);
    }
}
