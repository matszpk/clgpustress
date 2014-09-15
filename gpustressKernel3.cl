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

kernel void gpuStress(uint n, const global float4* restrict input,
                global float4* restrict output)
{
    size_t gid = get_global_id(0);
    size_t lid = get_local_id(0);
    
    float4 tmpValue1, tmpValue2, tmpValue3, tmpValue4;
    float4 tmp2Value1, tmp2Value2, tmp2Value3, tmp2Value4;
    float4 xValue1, xValue2, xValue3, xValue4;
    
    xValue1 = (float4)(gid, gid+1, gid+7, gid+22)*0.001f + 1.4f;
    xValue2 = (float4)(gid, gid+2, gid+3, gid+13)*0.0012f + 1.4f;
    xValue3 = (float4)(gid, gid+1, gid+5, gid+16)*0.0009f + 1.4f;
    xValue4 = (float4)(gid, gid+4, gid+7, gid+11)*0.0014f + 1.4f;
    
    float4 inValue1 = input[gid*4];
    float4 inValue2 = input[gid*4+1];
    float4 inValue3 = input[gid*4+2];
    float4 inValue4 = input[gid*4+3];
    
    for (uint j = 0; j < KITERSNUM; j++)
    {
        tmpValue1 = mad(inValue1, -inValue2, inValue3);
        tmpValue2 = mad(inValue2, inValue3, inValue4);
        tmpValue3 = mad(inValue3, -inValue4, inValue1);
        tmpValue4 = mad(inValue4, inValue1, inValue2);
        
        tmp2Value1 = mad(tmpValue1, tmpValue2, tmpValue3);
        tmp2Value2 = mad(tmpValue2, tmpValue3, tmpValue4);
        tmp2Value3 = mad(tmpValue3, tmpValue4, tmpValue1);
        tmp2Value4 = mad(tmpValue4, tmpValue1, tmpValue2);
        
        tmpValue1 = mad(tmp2Value1, -tmp2Value2, tmp2Value3);
        tmpValue2 = mad(tmp2Value2, tmp2Value3, -tmp2Value4);
        tmpValue3 = mad(tmp2Value3, -tmp2Value4, tmp2Value1);
        tmpValue4 = mad(tmp2Value4, tmp2Value1, -tmp2Value2);
        
        inValue1 = as_float4((as_int4(tmpValue1) & (0xc7ffffffU)) | 0x40000000U);
        inValue2 = as_float4((as_int4(tmpValue2) & (0xc7ffffffU)) | 0x40000000U);
        inValue3 = as_float4((as_int4(tmpValue3) & (0xc7ffffffU)) | 0x40000000U);
        inValue4 = as_float4((as_int4(tmpValue4) & (0xc7ffffffU)) | 0x40000000U);
    }
    
    output[gid*4] = inValue1;
    output[gid*4+1] = inValue2;
    output[gid*4+2] = inValue3;
    output[gid*4+3] = inValue4;
    
    barrier(CLK_GLOBAL_MEM_FENCE);
    
    const size_t gid2 = GROUPSIZE*get_group_id(0) + ((lid+1)%GROUPSIZE);
    inValue1 = mad(input[gid2*4+3], xValue1, inValue1);
    inValue2 = mad(input[gid2*4+1], xValue2, inValue2);
    inValue3 = mad(input[gid2*4+2], xValue3, inValue3);
    inValue4 = mad(input[gid2*4+0], xValue4, inValue4);
    
    for (uint j = 0; j < KITERSNUM; j++)
    {
        tmpValue1 = mad(inValue1, -inValue2, inValue3);
        tmpValue2 = mad(inValue2, inValue3, inValue4);
        tmpValue3 = mad(inValue3, -inValue4, inValue1);
        tmpValue4 = mad(inValue4, inValue1, inValue2);
        
        tmp2Value1 = mad(tmpValue1, tmpValue2, tmpValue3);
        tmp2Value2 = mad(tmpValue2, tmpValue3, tmpValue4);
        tmp2Value3 = mad(tmpValue3, tmpValue4, tmpValue1);
        tmp2Value4 = mad(tmpValue4, tmpValue1, tmpValue2);
        
        tmpValue1 = mad(tmp2Value1, -tmp2Value2, tmp2Value3);
        tmpValue2 = mad(tmp2Value2, tmp2Value3, -tmp2Value4);
        tmpValue3 = mad(tmp2Value3, -tmp2Value4, tmp2Value1);
        tmpValue4 = mad(tmp2Value4, tmp2Value1, -tmp2Value2);
        
        inValue1 = as_float4((as_int4(tmpValue1) & (0xc7ffffffU)) | 0x40000000U);
        inValue2 = as_float4((as_int4(tmpValue2) & (0xc7ffffffU)) | 0x40000000U);
        inValue3 = as_float4((as_int4(tmpValue3) & (0xc7ffffffU)) | 0x40000000U);
        inValue4 = as_float4((as_int4(tmpValue4) & (0xc7ffffffU)) | 0x40000000U);
    }
    
    output[gid2*4] = inValue1;
    output[gid2*4+1] = inValue2;
    output[gid2*4+2] = inValue3;
    output[gid2*4+3] = inValue4;
    
    barrier(CLK_GLOBAL_MEM_FENCE);
    
    const size_t gid3 = GROUPSIZE*get_group_id(0) + ((lid+2)%GROUPSIZE);
    inValue1 = mad(input[gid3*4], xValue1, inValue1);
    inValue2 = mad(input[gid3*4+1], xValue2, inValue2);
    inValue3 = mad(input[gid3*4+2], xValue3, inValue3);
    inValue4 = mad(input[gid3*4+3], xValue4, inValue4);
    
    for (uint j = 0; j < KITERSNUM; j++)
    {
        tmpValue1 = mad(inValue1, -inValue2, inValue3);
        tmpValue2 = mad(inValue2, inValue3, inValue4);
        tmpValue3 = mad(inValue3, -inValue4, inValue1);
        tmpValue4 = mad(inValue4, inValue1, inValue2);
        
        tmp2Value1 = mad(tmpValue1, tmpValue2, tmpValue3);
        tmp2Value2 = mad(tmpValue2, tmpValue3, tmpValue4);
        tmp2Value3 = mad(tmpValue3, tmpValue4, tmpValue1);
        tmp2Value4 = mad(tmpValue4, tmpValue1, tmpValue2);
        
        tmpValue1 = mad(tmp2Value1, -tmp2Value2, tmp2Value3);
        tmpValue2 = mad(tmp2Value2, tmp2Value3, -tmp2Value4);
        tmpValue3 = mad(tmp2Value3, -tmp2Value4, tmp2Value1);
        tmpValue4 = mad(tmp2Value4, tmp2Value1, -tmp2Value2);
        
        inValue1 = as_float4((as_int4(tmpValue1) & (0xc7ffffffU)) | 0x40000000U);
        inValue2 = as_float4((as_int4(tmpValue2) & (0xc7ffffffU)) | 0x40000000U);
        inValue3 = as_float4((as_int4(tmpValue3) & (0xc7ffffffU)) | 0x40000000U);
        inValue4 = as_float4((as_int4(tmpValue4) & (0xc7ffffffU)) | 0x40000000U);
    }
    
    output[gid3*4] = inValue1;
    output[gid3*4+1] = inValue2;
    output[gid3*4+2] = inValue3;
    output[gid3*4+3] = inValue4;
}
