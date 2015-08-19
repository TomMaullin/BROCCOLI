/*
    BROCCOLI: Software for Fast fMRI Analysis on Many-Core CPUs and GPUs
    Copyright (C) <2013>  Anders Eklund, andek034@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
    PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
    FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
    OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.
*/

// Help functions
int Calculate2DIndex(int x, int y, int DATA_W)
{
	return x + y * DATA_W;
}

int Calculate3DIndex(int x, int y, int z, int DATA_W, int DATA_H)
{
	return x + y * DATA_W + z * DATA_W * DATA_H;
}

int Calculate4DIndex(int x, int y, int z, int t, int DATA_W, int DATA_H, int DATA_D)
{
	return x + y * DATA_W + z * DATA_W * DATA_H + t * DATA_W * DATA_H * DATA_D;
}





// Statistical functions

// General function for calculating beta weights, all voxels use the same design matrix, not optimized for speed


__kernel void CalculateBetaWeightsGLM(__global float* Beta_Volumes, 
                                      __global const float* Volumes, 
									  __global const float* Mask, 
									  __constant float* c_xtxxt_GLM, 
									  __constant float* c_Censored_Timepoints,
									  __private int DATA_W, 
									  __private int DATA_H, 
									  __private int DATA_D, 
									  __private int NUMBER_OF_VOLUMES, 
									  __private int NUMBER_OF_REGRESSORS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		return;
	}

	int t = 0;
	float beta[25];
	
	// Reset beta weights
	beta[0] = 0.0f;
	beta[1] = 0.0f;
	beta[2] = 0.0f;
	beta[3] = 0.0f;
	beta[4] = 0.0f;
	beta[5] = 0.0f;
	beta[6] = 0.0f;
	beta[7] = 0.0f;
	beta[8] = 0.0f;
	beta[9] = 0.0f;
	beta[10] = 0.0f;
	beta[11] = 0.0f;
	beta[12] = 0.0f;
	beta[13] = 0.0f;
	beta[14] = 0.0f;
	beta[15] = 0.0f;
	beta[16] = 0.0f;
	beta[17] = 0.0f;
	beta[18] = 0.0f;
	beta[19] = 0.0f;
	beta[20] = 0.0f;
	beta[21] = 0.0f;
	beta[22] = 0.0f;
	beta[23] = 0.0f;
	beta[24] = 0.0f;

	// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
	// Loop over volumes
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		float temp = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] * c_Censored_Timepoints[v];

		// Loop over regressors
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			beta[r] += temp * c_xtxxt_GLM[NUMBER_OF_VOLUMES * r + v];
		}
	}

	// Save beta values
	for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{
		Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)] = beta[r];
	}
}

__kernel void CalculateBetaWeightsGLMSlice(__global float* Beta_Volumes, 
                                      	   __global const float* Volumes, 
									       __global const float* Mask, 
									       __global const float* c_xtxxt_GLM, 
									       __constant float* c_Censored_Timepoints,
									       __private int DATA_W, 
									       __private int DATA_H, 
									       __private int DATA_D, 
									       __private int NUMBER_OF_VOLUMES, 
									       __private int NUMBER_OF_REGRESSORS,
									       __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	int NUMBER_OF_REGRESSORS_PER_CHUNK = 25;
	int REGRESSOR_GROUPS = (int)ceil((float)NUMBER_OF_REGRESSORS / (float)NUMBER_OF_REGRESSORS_PER_CHUNK);
	int NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 0;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		return;
	}

	// Loop over chunks of 25 regressors at a time, since it is not possible to use for example 400 registers per thread
	for (int regressor_group = 0; regressor_group < REGRESSOR_GROUPS; regressor_group++)
	{
		// Check how many regressors that are left
		if ( (NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) >= 25 )
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 25;
		}	
		else
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK;
		}

		int t = 0;
		float beta[25];
	
		// Reset beta weights
		beta[0] = 0.0f;
		beta[1] = 0.0f;
		beta[2] = 0.0f;
		beta[3] = 0.0f;
		beta[4] = 0.0f;
		beta[5] = 0.0f;
		beta[6] = 0.0f;
		beta[7] = 0.0f;
		beta[8] = 0.0f;
		beta[9] = 0.0f;
		beta[10] = 0.0f;
		beta[11] = 0.0f;
		beta[12] = 0.0f;
		beta[13] = 0.0f;
		beta[14] = 0.0f;
		beta[15] = 0.0f;
		beta[16] = 0.0f;
		beta[17] = 0.0f;
		beta[18] = 0.0f;
		beta[19] = 0.0f;
		beta[20] = 0.0f;
		beta[21] = 0.0f;
		beta[22] = 0.0f;
		beta[23] = 0.0f;
		beta[24] = 0.0f;

		// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
		// Loop over volumes
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			float temp = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] * c_Censored_Timepoints[v];

			// Loop over regressors
			for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
			{
				beta[r] += temp * c_xtxxt_GLM[NUMBER_OF_VOLUMES * (r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) + v];
			}
		}

		// Save beta values for current chunk
		for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,slice,r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK,DATA_W,DATA_H,DATA_D)] = beta[r];
		}
	}
}



__kernel void CalculateBetaWeightsAndContrastsGLM(__global float* Beta_Volumes, 
                                      	   		  __global float* Contrast_Volumes, 
                                      	   		  __global const float* Volumes, 
									       		  __global const float* Mask, 
									       		  __global const float* c_xtxxt_GLM, 
												  __global const float* c_Contrasts,
									       		  __constant float* c_Censored_Timepoints,
									       		  __private int DATA_W, 
									       		  __private int DATA_H, 
									       		  __private int DATA_D, 
									       		  __private int NUMBER_OF_VOLUMES, 
									      		  __private int NUMBER_OF_REGRESSORS,
									      		  __private int NUMBER_OF_CONTRASTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	int NUMBER_OF_REGRESSORS_PER_CHUNK = 25;
	int REGRESSOR_GROUPS = (int)ceil((float)NUMBER_OF_REGRESSORS / (float)NUMBER_OF_REGRESSORS_PER_CHUNK);
	int NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 0;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			Contrast_Volumes[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		return;
	}

	float beta[25];

	// Loop over chunks of 25 regressors at a time, since it is not possible to use for example 400 registers per thread
	for (int regressor_group = 0; regressor_group < REGRESSOR_GROUPS; regressor_group++)
	{
		// Check how many regressors that are left
		if ( (NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) >= 25 )
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 25;
		}	
		else
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK;
		}

		int t = 0;		
	
		// Reset beta weights
		beta[0] = 0.0f;
		beta[1] = 0.0f;
		beta[2] = 0.0f;
		beta[3] = 0.0f;
		beta[4] = 0.0f;
		beta[5] = 0.0f;
		beta[6] = 0.0f;
		beta[7] = 0.0f;
		beta[8] = 0.0f;
		beta[9] = 0.0f;
		beta[10] = 0.0f;
		beta[11] = 0.0f;
		beta[12] = 0.0f;
		beta[13] = 0.0f;
		beta[14] = 0.0f;
		beta[15] = 0.0f;
		beta[16] = 0.0f;
		beta[17] = 0.0f;
		beta[18] = 0.0f;
		beta[19] = 0.0f;
		beta[20] = 0.0f;
		beta[21] = 0.0f;
		beta[22] = 0.0f;
		beta[23] = 0.0f;
		beta[24] = 0.0f;

		// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
		// Loop over volumes
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			float temp = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] * c_Censored_Timepoints[v];

			// Loop over regressors
			for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
			{
				beta[r] += temp * c_xtxxt_GLM[NUMBER_OF_VOLUMES * (r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) + v];
			}
		}

		// Save beta values for current chunk
		for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,z,r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK,DATA_W,DATA_H,DATA_D)] = beta[r];
		}
	}

	if (NUMBER_OF_REGRESSORS <= 25)
	{
		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
		}
	}
	else
	{
		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
		}
	}
}



__kernel void CalculateBetaWeightsAndContrastsGLMSlice(__global float* Beta_Volumes, 
                                      	   			   __global float* Contrast_Volumes, 
                                      	   			   __global const float* Volumes, 
									       			   __global const float* Mask, 
									       			   __global const float* c_xtxxt_GLM, 
													   __global const float* c_Contrasts,
									       			   __constant float* c_Censored_Timepoints,
									       			   __private int DATA_W, 
									       			   __private int DATA_H, 
									       			   __private int DATA_D, 
									       			   __private int NUMBER_OF_VOLUMES, 
									      			   __private int NUMBER_OF_REGRESSORS,
									      			   __private int NUMBER_OF_CONTRASTS,
									       			   __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	int NUMBER_OF_REGRESSORS_PER_CHUNK = 25;
	int REGRESSOR_GROUPS = (int)ceil((float)NUMBER_OF_REGRESSORS / (float)NUMBER_OF_REGRESSORS_PER_CHUNK);
	int NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 0;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			Contrast_Volumes[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		return;
	}

	float beta[25];

	// Loop over chunks of 25 regressors at a time, since it is not possible to use for example 400 registers per thread
	for (int regressor_group = 0; regressor_group < REGRESSOR_GROUPS; regressor_group++)
	{
		// Check how many regressors that are left
		if ( (NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) >= 25 )
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 25;
		}	
		else
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK;
		}

		int t = 0;		
	
		// Reset beta weights
		beta[0] = 0.0f;
		beta[1] = 0.0f;
		beta[2] = 0.0f;
		beta[3] = 0.0f;
		beta[4] = 0.0f;
		beta[5] = 0.0f;
		beta[6] = 0.0f;
		beta[7] = 0.0f;
		beta[8] = 0.0f;
		beta[9] = 0.0f;
		beta[10] = 0.0f;
		beta[11] = 0.0f;
		beta[12] = 0.0f;
		beta[13] = 0.0f;
		beta[14] = 0.0f;
		beta[15] = 0.0f;
		beta[16] = 0.0f;
		beta[17] = 0.0f;
		beta[18] = 0.0f;
		beta[19] = 0.0f;
		beta[20] = 0.0f;
		beta[21] = 0.0f;
		beta[22] = 0.0f;
		beta[23] = 0.0f;
		beta[24] = 0.0f;

		// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
		// Loop over volumes
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			float temp = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] * c_Censored_Timepoints[v];

			// Loop over regressors
			for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
			{
				beta[r] += temp * c_xtxxt_GLM[NUMBER_OF_VOLUMES * (r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) + v];
			}
		}

		// Save beta values for current chunk
		for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,slice,r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK,DATA_W,DATA_H,DATA_D)] = beta[r];
		}
	}

	if (NUMBER_OF_REGRESSORS <= 25)
	{
		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
		}
	}
	else
	{
		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
		}
	}
}

// Special function for calculating beta weights, all voxels use different design matrices (needed for Cochrane-Orcutt procedure)


__kernel void CalculateBetaWeightsGLMFirstLevel(__global float* Beta_Volumes, 
												__global const float* Volumes, 
												__global const float* Mask, 
												__global const float* d_xtxxt_GLM, 
												__global const float* d_Voxel_Numbers, 
												__constant float* c_Censored_Timepoints,
												__private int DATA_W, 
												__private int DATA_H, 
												__private int DATA_D, 
												__private int NUMBER_OF_VOLUMES, 
												__private int NUMBER_OF_REGRESSORS,
												__private int NUMBER_OF_INVALID_TIMEPOINTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	int NUMBER_OF_REGRESSORS_PER_CHUNK = 25;
	int REGRESSOR_GROUPS = (int)ceil((float)NUMBER_OF_REGRESSORS / (float)NUMBER_OF_REGRESSORS_PER_CHUNK);
	int NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 0;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		return;
	}

	// Loop over chunks of 25 regressors at a time, since it is not possible to use for example 400 registers per thread
	for (int regressor_group = 0; regressor_group < REGRESSOR_GROUPS; regressor_group++)
	{
		// Check how many regressors that are left
		if ( (NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) >= 25 )
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 25;
		}	
		else
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK;
		}

		int t = 0;
		float beta[25];
	
		// Reset beta weights
		beta[0] = 0.0f;
		beta[1] = 0.0f;
		beta[2] = 0.0f;
		beta[3] = 0.0f;
		beta[4] = 0.0f;
		beta[5] = 0.0f;
		beta[6] = 0.0f;
		beta[7] = 0.0f;
		beta[8] = 0.0f;
		beta[9] = 0.0f;
		beta[10] = 0.0f;
		beta[11] = 0.0f;
		beta[12] = 0.0f;
		beta[13] = 0.0f;
		beta[14] = 0.0f;
		beta[15] = 0.0f;
		beta[16] = 0.0f;
		beta[17] = 0.0f;
		beta[18] = 0.0f;
		beta[19] = 0.0f;
		beta[20] = 0.0f;
		beta[21] = 0.0f;
		beta[22] = 0.0f;
		beta[23] = 0.0f;
		beta[24] = 0.0f;

		// Get the specific voxel number for this brain voxel
		int voxel_number = (int)d_Voxel_Numbers[Calculate3DIndex(x,y,z,DATA_W,DATA_H)];
		
		// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
		// Loop over volumes
		for (int v = NUMBER_OF_INVALID_TIMEPOINTS; v < NUMBER_OF_VOLUMES; v++)
		{
			float temp = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

			// Loop over regressors
			for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
			{
				beta[r] += temp * d_xtxxt_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * (r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) + v];
			}
		}

		// Save beta values for the current chunk of regressors
		for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,z,r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK,DATA_W,DATA_H,DATA_D)] = beta[r];
		}
	}
}

__kernel void CalculateBetaWeightsGLMFirstLevelSlice(__global float* Beta_Volumes, 
												     __global const float* Volumes, 
												     __global const float* Mask, 
												     __global const float* d_xtxxt_GLM, 
												     __global const float* d_Voxel_Numbers, 
												     __constant float* c_Censored_Timepoints,
												     __private int DATA_W, 
												     __private int DATA_H, 
												     __private int DATA_D, 
												     __private int NUMBER_OF_VOLUMES, 
												     __private int NUMBER_OF_REGRESSORS,
												     __private int NUMBER_OF_INVALID_TIMEPOINTS,
												     __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	int NUMBER_OF_REGRESSORS_PER_CHUNK = 25;
	int REGRESSOR_GROUPS = (int)ceil((float)NUMBER_OF_REGRESSORS / (float)NUMBER_OF_REGRESSORS_PER_CHUNK);
	int NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 0;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
		return;
	}

	// Loop over chunks of 25 regressors at a time, since it is not possible to use for example 400 registers per thread
	for (int regressor_group = 0; regressor_group < REGRESSOR_GROUPS; regressor_group++)
	{
		// Check how many regressors that are left
		if ( (NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) >= 25 )
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = 25;
		}	
		else
		{
			NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK = NUMBER_OF_REGRESSORS - regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK;
		}

		int t = 0;
		float beta[25];
	
		// Reset beta weights
		beta[0] = 0.0f;
		beta[1] = 0.0f;
		beta[2] = 0.0f;
		beta[3] = 0.0f;
		beta[4] = 0.0f;
		beta[5] = 0.0f;
		beta[6] = 0.0f;
		beta[7] = 0.0f;
		beta[8] = 0.0f;
		beta[9] = 0.0f;
		beta[10] = 0.0f;
		beta[11] = 0.0f;
		beta[12] = 0.0f;
		beta[13] = 0.0f;
		beta[14] = 0.0f;
		beta[15] = 0.0f;
		beta[16] = 0.0f;
		beta[17] = 0.0f;
		beta[18] = 0.0f;
		beta[19] = 0.0f;
		beta[20] = 0.0f;
		beta[21] = 0.0f;
		beta[22] = 0.0f;
		beta[23] = 0.0f;
		beta[24] = 0.0f;

		// Get the specific voxel number for this brain voxel
		//int voxel_number = (int)d_Voxel_Numbers[Calculate3DIndex(x,y,z,DATA_W,DATA_H)];
		int voxel_number = (int)d_Voxel_Numbers[Calculate2DIndex(x,y,DATA_W)];

		// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
		// Loop over volumes
		for (int v = NUMBER_OF_INVALID_TIMEPOINTS; v < NUMBER_OF_VOLUMES; v++)
		{
			//float temp = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
			float temp = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];

			// Loop over regressors
			for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
			{
				beta[r] += temp * d_xtxxt_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * (r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK) + v];
			}
		}

		// Save beta values for the current chunk of regressors
		for (int r = 0; r < NUMBER_OF_REGRESSORS_IN_CURRENT_CHUNK; r++)
		{
			Beta_Volumes[Calculate4DIndex(x,y,slice,r + regressor_group * NUMBER_OF_REGRESSORS_PER_CHUNK,DATA_W,DATA_H,DATA_D)] = beta[r];
		}
	}
}




__kernel void CalculateGLMResiduals(__global float* Residuals,
		                            __global const float* Volumes,
		                            __global const float* Beta_Volumes,
		                            __global const float* Mask,
		                            __global const float *c_X_GLM,
		                            __private int DATA_W,
		                            __private int DATA_H,
		                            __private int DATA_D,
		                            __private int NUMBER_OF_VOLUMES,
		                            __private int NUMBER_OF_REGRESSORS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;

	// Special case for low number of regressors, store beta scores in registers for faster performance
	if (NUMBER_OF_REGRESSORS <= 25)
	{
		float beta[25];

		// Load beta values into registers
	    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			beta[r] = Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
		}

		// Calculate the residual
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
			}

			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;			
		}
	}
	// General case for large number of regressors (slower)
	else
	{
		// Calculate the residual
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
			}

			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;			
		}
	}
}


__kernel void CalculateGLMResidualsSlice(__global float* Residuals,
		                                 __global const float* Volumes,
		                                 __global const float* Beta_Volumes,
		                                 __global const float* Mask,
		                                 __global const float *c_X_GLM,
		                                 __private int DATA_W,
		                                 __private int DATA_H,
		                                 __private int DATA_D,
		                                 __private int NUMBER_OF_VOLUMES,
		                                 __private int NUMBER_OF_REGRESSORS,
                                         __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;

	// Special case for low number of regressors, store beta scores in registers for faster performance
	if (NUMBER_OF_REGRESSORS <= 25)
	{
		float beta[25];

		// Load beta values into registers
	    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			beta[r] = Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
		}

		// Calculate the residual
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
			}

			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = eps;
		}
	}
	// General case for large number of regressors (slower)
	else
	{
		// Calculate the residual
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
			}

			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = eps;
		}
	}
}



__kernel void CalculateStatisticalMapsGLMTTestFirstLevel(__global float* Statistical_Maps,
														 __global float* Contrast_Volumes,
		                                       	   	   	 __global float* Residuals,
		                                       	   	   	 __global float* Residual_Variances,
		                                       	   	   	 __global const float* Volumes,
		                                       	   	   	 __global const float* Beta_Volumes,
		                                       	   	   	 __global const float* Mask,
		                                       	   	   	 __global const float* d_X_GLM,
		                                       	   	   	 __global const float* d_GLM_Scalars,
		                                       	   	   	 __global const float* d_Voxel_Numbers,
		                                       	   	   	 __constant float* c_Contrasts,
		                                       	   	   	 __constant float* c_Censored_Timepoints,
		                                       	   	   	 __private int DATA_W,
		                                       	   	   	 __private int DATA_H,
		                                       	   	   	 __private int DATA_D,
		                                       	   	   	 __private int NUMBER_OF_VOLUMES,
		                                       	   	   	 __private int NUMBER_OF_REGRESSORS,
		                                       	   	   	 __private int NUMBER_OF_CONTRASTS,
		                                       	   	   	 __private int NUMBER_OF_CENSORED_TIMEPOINTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = 0.0f;

		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			Contrast_Volumes[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
			Statistical_Maps[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;

    // Get the specific voxel number for this brain voxel
    int voxel_number = (int)d_Voxel_Numbers[Calculate3DIndex(x,y,z,DATA_W,DATA_H)];
	
	// Special case for a low number of regressors
	if (NUMBER_OF_REGRESSORS <= 25)
	{
		float beta[25];
		
		// Load beta values into registers
	    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			beta[r] = Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
		}

		// Calculate the mean of the error eps, using voxel-specific design models
		meaneps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
			}
			eps *= c_Censored_Timepoints[v];
			meaneps += eps;
	
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;		
		}
		meaneps /= ((float)NUMBER_OF_VOLUMES);

		// Now calculate the variance of eps, using voxel-specific design models
		vareps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
			}
			vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
		}
		vareps /= ((float)NUMBER_OF_VOLUMES - 1.0f);
		Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = vareps;

		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
			Statistical_Maps[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value * rsqrt(vareps * d_GLM_Scalars[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)]);
		}
	}
	// General case for large number of regressors, slower
	else
	{
		// Calculate the mean of the error eps, using voxel-specific design models
		meaneps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
			}
			eps *= c_Censored_Timepoints[v];
			meaneps += eps;
	
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;		
		}
		meaneps /= ((float)NUMBER_OF_VOLUMES);

		// Now calculate the variance of eps, using voxel-specific design models
		vareps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
			}
			vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
		}
		vareps /= ((float)NUMBER_OF_VOLUMES - 1.0f);
		Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = vareps;

		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
			Statistical_Maps[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value * rsqrt(vareps * d_GLM_Scalars[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)]);
		}
	}
}


__kernel void CalculateStatisticalMapsGLMTTestFirstLevelSlice(__global float* Statistical_Maps,
														      __global float* Contrast_Volumes,
		                                       	   	   	      __global float* Residuals,
		                                       	   	   	      __global float* Residual_Variances,
		                                       	   	   	      __global const float* Volumes,
		                                       	   	   	      __global const float* Beta_Volumes,
		                                       	   	   	      __global const float* Mask,
		                                       	   	   	      __global const float* d_X_GLM,
		                                       	   	   	      __global const float* d_GLM_Scalars,
		                                       	   	   	      __global const float* d_Voxel_Numbers,
		                                       	   	   	      __constant float* c_Contrasts,
		                                       	   	   	      __constant float* c_Censored_Timepoints,
		                                       	   	   	      __private int DATA_W,
		                                       	   	   	      __private int DATA_H,
		                                       	   	   	      __private int DATA_D,
		                                       	   	   	      __private int NUMBER_OF_VOLUMES,
		                                       	   	   	      __private int NUMBER_OF_REGRESSORS,
		                                       	   	   	      __private int NUMBER_OF_CONTRASTS,
		                                       	   	   	      __private int NUMBER_OF_CENSORED_TIMEPOINTS,
                                                              __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		Residual_Variances[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] = 0.0f;

		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			Contrast_Volumes[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
			Statistical_Maps[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			//Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;

    // Get the specific voxel number for this brain voxel
    //int voxel_number = (int)d_Voxel_Numbers[Calculate3DIndex(x,y,z,DATA_W,DATA_H)];
	int voxel_number = (int)d_Voxel_Numbers[Calculate2DIndex(x,y,DATA_W)];

	// Special case for a low number of regressors
	if (NUMBER_OF_REGRESSORS <= 25)
	{
		float beta[25];
		
		// Load beta values into registers
	    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			beta[r] = Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
		}

		// Calculate the mean of the error eps, using voxel-specific design models
		meaneps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
				//eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
			}
			eps *= c_Censored_Timepoints[v];
			meaneps += eps;
	
			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = eps;		
		}
		meaneps /= ((float)NUMBER_OF_VOLUMES);

		// Now calculate the variance of eps, using voxel-specific design models
		vareps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
				//eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
			}
			vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
		}
		vareps /= ((float)NUMBER_OF_VOLUMES - 1.0f);
		Residual_Variances[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] = vareps;

		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
			Statistical_Maps[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = contrast_value * rsqrt(vareps * d_GLM_Scalars[Calculate3DIndex(x,y,c,DATA_W,DATA_H)]);
		}
	}
	// General case for large number of regressors, slower
	else
	{
		// Calculate the mean of the error eps, using voxel-specific design models
		meaneps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
				//eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
			}
			eps *= c_Censored_Timepoints[v];
			meaneps += eps;
	
			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = eps;		
		}
		meaneps /= ((float)NUMBER_OF_VOLUMES);

		// Now calculate the variance of eps, using voxel-specific design models
		vareps = 0.0f;
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];

			// Calculate eps
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
				//eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
			}
			vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
		}
		vareps /= ((float)NUMBER_OF_VOLUMES - 1.0f);
		Residual_Variances[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] = vareps;

		// Loop over contrasts and calculate t-values, using a voxel-specific GLM scalar
		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			float contrast_value = 0.0f;
			for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
			{
				contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
			}
			Contrast_Volumes[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = contrast_value;
			Statistical_Maps[Calculate4DIndex(x,y,slice,c,DATA_W,DATA_H,DATA_D)] = contrast_value * rsqrt(vareps * d_GLM_Scalars[Calculate3DIndex(x,y,c,DATA_W,DATA_H)]);
		}
	}
}





__kernel void CalculateStatisticalMapsGLMFTestFirstLevel(__global float* Statistical_Maps,
		                                       	   	   	 __global float* Residuals,
		                                       	   	   	 __global float* Residual_Variances,
		                                       	   	   	 __global const float* Volumes,
		                                       	   	   	 __global const float* Beta_Volumes,
		                                       	   	   	 __global const float* Mask,
		                                       	   	   	 __global const float* d_X_GLM,
		                                       	   	   	 __global const float* d_GLM_Scalars,
		                                       	   	   	 __global const float* d_Voxel_Numbers,
		                                       	   	   	 __constant float* c_Contrasts,
		                                       	   	   	 __constant float* c_Censored_Timepoints,
		                                       	   	   	 __private int DATA_W,
		                                       	   	   	 __private int DATA_H,
		                                       	   	   	 __private int DATA_D,
		                                       	   	   	 __private int NUMBER_OF_VOLUMES,
		                                       	   	   	 __private int NUMBER_OF_REGRESSORS,
		                                       	   	   	 __private int NUMBER_OF_CONTRASTS,
		                                       	   	   	 __private int NUMBER_OF_CENSORED_TIMEPOINTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = 0.0f;

		Statistical_Maps[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)] = 0.0f;

		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;
	float beta[25];

	// Load beta values into registers
    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{
		beta[r] = Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
	}

    // Get the specific voxel number for this brain voxel
    int voxel_number = (int)d_Voxel_Numbers[Calculate3DIndex(x,y,z,DATA_W,DATA_H)];

	// Calculate the mean of the error eps, using voxel-specific design models
	meaneps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		eps *= c_Censored_Timepoints[v];
		meaneps += eps;
		Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;
	}
	//meaneps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_CENSORED_TIMEPOINTS);
	meaneps /= ((float)NUMBER_OF_VOLUMES);

	// Now calculate the variance of eps, using voxel-specific design models
	vareps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
	}
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - (float)NUMBER_OF_CENSORED_TIMEPOINTS - 1.0f);
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_CENSORED_TIMEPOINTS - 1.0f);
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - 1.0f);
	vareps /= ((float)NUMBER_OF_VOLUMES - 1.0f);
	Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = vareps;

	// Calculate matrix vector product C*beta (minus u)
	float cbeta[25];
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		cbeta[c] = 0.0f;
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
		}
	}

	// Calculate total vector matrix vector product (C*beta)^T ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)

	// Calculate right hand side, temp = ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		beta[c] = 0.0f;
		for (int cc = 0; cc < NUMBER_OF_CONTRASTS; cc++)
		{
			//beta[c] += 1.0f/vareps * c_ctxtxc_GLM[cc + c * NUMBER_OF_CONTRASTS] * cbeta[cc];
			beta[c] += 1.0f/vareps * d_GLM_Scalars[Calculate4DIndex(x,y,z,cc + c * NUMBER_OF_CONTRASTS,DATA_W,DATA_H,DATA_D)] * cbeta[cc];
		}
	}

	// Finally calculate (C*beta)^T * temp
	float scalar = 0.0f;
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		scalar += cbeta[c] * beta[c];
	}

	// Save F-value
	Statistical_Maps[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = scalar/(float)NUMBER_OF_CONTRASTS;
}



__kernel void CalculateStatisticalMapsGLMFTestFirstLevelSlice(__global float* Statistical_Maps,
		                                       	   	   	      __global float* Residuals,
		                                       	   	   	      __global float* Residual_Variances,
		                                       	   	   	      __global const float* Volumes,
		                                       	   	   	      __global const float* Beta_Volumes,
		                                       	   	   	      __global const float* Mask,
		                                       	   	   	      __global const float* d_X_GLM,
		                                       	   	   	      __global const float* d_GLM_Scalars,
		                                       	   	   	      __global const float* d_Voxel_Numbers,
		                                       	   	   	      __constant float* c_Contrasts,
		                                       	   	   	      __constant float* c_Censored_Timepoints,
		                                       	   	   	      __private int DATA_W,
		                                       	   	   	      __private int DATA_H,
		                                       	   	   	      __private int DATA_D,
		                                       	   	   	      __private int NUMBER_OF_VOLUMES,
		                                       	   	   	      __private int NUMBER_OF_REGRESSORS,
		                                       	   	   	      __private int NUMBER_OF_CONTRASTS,
		                                       	   	   	      __private int NUMBER_OF_CENSORED_TIMEPOINTS,
 	 	 												      __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	// First deal with voxels outside the mask
	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		Residual_Variances[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] = 0.0f;

		Statistical_Maps[Calculate4DIndex(x,y,slice,0,DATA_W,DATA_H,DATA_D)] = 0.0f;

		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;
	float beta[25];

	// Load beta values into registers
    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{
		beta[r] = Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
	}

    // Get the specific voxel number for this brain voxel
    int voxel_number = (int)d_Voxel_Numbers[Calculate2DIndex(x,y,DATA_W)];

	// Calculate the mean of the error eps, using voxel-specific design models
	meaneps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
			//eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		eps *= c_Censored_Timepoints[v];
		meaneps += eps;
		Residuals[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = eps;
	}
	//meaneps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_CENSORED_TIMEPOINTS);
	meaneps /= ((float)NUMBER_OF_VOLUMES);

	// Now calculate the variance of eps, using voxel-specific design models
	vareps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= d_X_GLM[voxel_number * NUMBER_OF_VOLUMES * NUMBER_OF_REGRESSORS + NUMBER_OF_VOLUMES * r + v] * beta[r];
			//eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
	}
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - (float)NUMBER_OF_CENSORED_TIMEPOINTS - 1.0f);
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_CENSORED_TIMEPOINTS - 1.0f);
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - 1.0f);
	vareps /= ((float)NUMBER_OF_VOLUMES - 1.0f);
	Residual_Variances[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] = vareps;

	// Calculate matrix vector product C*beta (minus u)
	float cbeta[25];
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		cbeta[c] = 0.0f;
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
		}
	}

	// Calculate total vector matrix vector product (C*beta)^T ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)

	// Calculate right hand side, temp = ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		beta[c] = 0.0f;
		for (int cc = 0; cc < NUMBER_OF_CONTRASTS; cc++)
		{
			//beta[c] += 1.0f/vareps * c_ctxtxc_GLM[cc + c * NUMBER_OF_CONTRASTS] * cbeta[cc];
			beta[c] += 1.0f/vareps * d_GLM_Scalars[Calculate3DIndex(x,y,cc + c * NUMBER_OF_CONTRASTS,DATA_W,DATA_H)] * cbeta[cc];
		}
	}

	// Finally calculate (C*beta)^T * temp
	float scalar = 0.0f;
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		scalar += cbeta[c] * beta[c];
	}

	// Save F-value
	Statistical_Maps[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] = scalar/(float)NUMBER_OF_CONTRASTS;
}

// Unoptimized kernel for calculating t-values, not a problem for regular first and second level analysis

__kernel void CalculateStatisticalMapsGLMTTest(__global float* Statistical_Maps,
		                                       __global float* Residuals,
		                                       __global float* Residual_Variances,
		                                       __global const float* Volumes,
		                                       __global const float* Beta_Volumes,
		                                       __global const float* Mask,
		                                       __constant float *c_X_GLM,
		                                       __constant float* c_Contrasts,
		                                       __constant float* c_ctxtxc_GLM,
											   __constant float* c_Censored_Timepoints,
		                                       __private int DATA_W,
		                                       __private int DATA_H,
		                                       __private int DATA_D,
		                                       __private int NUMBER_OF_VOLUMES,
		                                       __private int NUMBER_OF_REGRESSORS,
		                                       __private int NUMBER_OF_CONTRASTS,
											   __private int NUMBER_OF_CENSORED_TIMEPOINTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = 0.0f;

		for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
		{
			Statistical_Maps[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}
	
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;
	float beta[25];

	// Load beta values into registers
    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{ 
		beta[r] = Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
	}

	// Calculate the mean of the error eps
	meaneps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{ 
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		//eps *= c_Censored_Timepoints[v];
		meaneps += eps;
		Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;
	}
	//meaneps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_CENSORED_TIMEPOINTS);
	meaneps /= ((float)NUMBER_OF_VOLUMES);


	// Now calculate the variance of eps
	vareps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		//vareps += (eps - meaneps) * (eps - meaneps) * c_Censored_Timepoints[v];
		//vareps += (eps - meaneps) * (eps - meaneps);
		vareps += eps*eps;
	}
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - (float)NUMBER_OF_CENSORED_TIMEPOINTS - 1.0f);
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_CENSORED_TIMEPOINTS - 1.0f);
	//vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - 1.0f);
	vareps /= ((float)NUMBER_OF_VOLUMES - NUMBER_OF_REGRESSORS);
	Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = vareps;
	
	// Loop over contrasts and calculate t-values
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		float contrast_value = 0.0f;
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
		}
		Statistical_Maps[Calculate4DIndex(x,y,z,c,DATA_W,DATA_H,DATA_D)] = contrast_value * rsqrt(vareps * c_ctxtxc_GLM[c]);
	}
}

// Unoptimized kernel for calculating F-values, not a problem for regular first and second level analysis

__kernel void CalculateStatisticalMapsGLMFTest(__global float* Statistical_Maps,
		                                       __global float* Residuals,
		                                       __global float* Residual_Variances,
		                                       __global const float* Volumes,
		                                       __global const float* Beta_Volumes,
		                                       __global const float* Mask,
		                                       __constant float* c_X_GLM,
		                                       __constant float* c_Contrasts,
		                                       __constant float* c_ctxtxc_GLM,
		                                       __constant float* c_Censored_Timepoints,
		                                       __private int DATA_W,
		                                       __private int DATA_H,
		                                       __private int DATA_D,
		                                       __private int NUMBER_OF_VOLUMES,
		                                       __private int NUMBER_OF_REGRESSORS,
		                                       __private int NUMBER_OF_CONTRASTS,
		                                       __private int NUMBER_OF_CENSORED_TIMEPOINTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = 0.0f;

		Statistical_Maps[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = 0.0f;
		
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		return;
	}

	int t = 0;
	float eps, meaneps, vareps;
	float beta[25];

	// Load beta values into registers
    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{
		beta[r] = Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
	}

	// Calculate the mean of the error eps
	meaneps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		meaneps += eps;
		Residuals[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;
	}
	meaneps /= (float)NUMBER_OF_VOLUMES;

	// Now calculate the variance of eps
	vareps = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * r + v] * beta[r];
		}
		vareps += (eps - meaneps) * (eps - meaneps);
	}
	vareps /= ((float)NUMBER_OF_VOLUMES - (float)NUMBER_OF_REGRESSORS - 1.0f); 
	Residual_Variances[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = vareps;

	//-------------------------

	// Calculate matrix vector product C*beta (minus u)
	float cbeta[25];
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		cbeta[c] = 0.0f;
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + r] * beta[r];
		}
	}

	// Calculate total vector matrix vector product (C*beta)^T ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)

	// Calculate right hand side, temp = ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		beta[c] = 0.0f;
		for (int cc = 0; cc < NUMBER_OF_CONTRASTS; cc++)
		{
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[cc + c * NUMBER_OF_CONTRASTS] * cbeta[cc];
		}
	}

	// Finally calculate (C*beta)^T * temp
	float scalar = 0.0f;
	for (int c = 0; c < NUMBER_OF_CONTRASTS; c++)
	{
		scalar += cbeta[c] * beta[c];
	}

	// Save F-value
	Statistical_Maps[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = scalar/(float)NUMBER_OF_CONTRASTS;
}
	




// Functions for permutation test

int LoadBetaWeights(__private float* beta, 
	                __global const float* Beta_Volumes, 
					int x, 
					int y, 
					int z, 
					int DATA_W, 
					int DATA_H, 
					int DATA_D, 
					int NUMBER_OF_REGRESSORS)
{
	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 2:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];

			break;

		case 3:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];

			break;

		case 4:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];

			break;

		case 5:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];

			break;

		case 6:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];

			break;

		case 7:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];			

			break;

		case 8:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];

			break;

		case 9:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];

			break;

		case 10:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];

			break;

		case 11:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];

			break;

		case 12:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];

			break;

		case 13:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 14:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 15:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 16:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 17:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 18:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 19:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];			

			break;

		case 20:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];
			beta[19] = Beta_Volumes[Calculate4DIndex(x,y,z,19,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 21:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];
			beta[19] = Beta_Volumes[Calculate4DIndex(x,y,z,19,DATA_W,DATA_H,DATA_D)];
			beta[20] = Beta_Volumes[Calculate4DIndex(x,y,z,20,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 22:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];
			beta[19] = Beta_Volumes[Calculate4DIndex(x,y,z,19,DATA_W,DATA_H,DATA_D)];
			beta[20] = Beta_Volumes[Calculate4DIndex(x,y,z,20,DATA_W,DATA_H,DATA_D)];
			beta[21] = Beta_Volumes[Calculate4DIndex(x,y,z,21,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 23:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];
			beta[19] = Beta_Volumes[Calculate4DIndex(x,y,z,19,DATA_W,DATA_H,DATA_D)];
			beta[20] = Beta_Volumes[Calculate4DIndex(x,y,z,20,DATA_W,DATA_H,DATA_D)];
			beta[21] = Beta_Volumes[Calculate4DIndex(x,y,z,21,DATA_W,DATA_H,DATA_D)];
			beta[22] = Beta_Volumes[Calculate4DIndex(x,y,z,22,DATA_W,DATA_H,DATA_D)];

			break;

		case 24:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];
			beta[19] = Beta_Volumes[Calculate4DIndex(x,y,z,19,DATA_W,DATA_H,DATA_D)];
			beta[20] = Beta_Volumes[Calculate4DIndex(x,y,z,20,DATA_W,DATA_H,DATA_D)];
			beta[21] = Beta_Volumes[Calculate4DIndex(x,y,z,21,DATA_W,DATA_H,DATA_D)];
			beta[22] = Beta_Volumes[Calculate4DIndex(x,y,z,22,DATA_W,DATA_H,DATA_D)];
			beta[23] = Beta_Volumes[Calculate4DIndex(x,y,z,23,DATA_W,DATA_H,DATA_D)];
			
			break;

		case 25:

			beta[0] = Beta_Volumes[Calculate4DIndex(x,y,z,0,DATA_W,DATA_H,DATA_D)];
			beta[1] = Beta_Volumes[Calculate4DIndex(x,y,z,1,DATA_W,DATA_H,DATA_D)];
			beta[2] = Beta_Volumes[Calculate4DIndex(x,y,z,2,DATA_W,DATA_H,DATA_D)];
			beta[3] = Beta_Volumes[Calculate4DIndex(x,y,z,3,DATA_W,DATA_H,DATA_D)];
			beta[4] = Beta_Volumes[Calculate4DIndex(x,y,z,4,DATA_W,DATA_H,DATA_D)];
			beta[5] = Beta_Volumes[Calculate4DIndex(x,y,z,5,DATA_W,DATA_H,DATA_D)];
			beta[6] = Beta_Volumes[Calculate4DIndex(x,y,z,6,DATA_W,DATA_H,DATA_D)];
			beta[7] = Beta_Volumes[Calculate4DIndex(x,y,z,7,DATA_W,DATA_H,DATA_D)];
			beta[8] = Beta_Volumes[Calculate4DIndex(x,y,z,8,DATA_W,DATA_H,DATA_D)];
			beta[9] = Beta_Volumes[Calculate4DIndex(x,y,z,9,DATA_W,DATA_H,DATA_D)];
			beta[10] = Beta_Volumes[Calculate4DIndex(x,y,z,10,DATA_W,DATA_H,DATA_D)];
			beta[11] = Beta_Volumes[Calculate4DIndex(x,y,z,11,DATA_W,DATA_H,DATA_D)];
			beta[12] = Beta_Volumes[Calculate4DIndex(x,y,z,12,DATA_W,DATA_H,DATA_D)];
			beta[13] = Beta_Volumes[Calculate4DIndex(x,y,z,13,DATA_W,DATA_H,DATA_D)];
			beta[14] = Beta_Volumes[Calculate4DIndex(x,y,z,14,DATA_W,DATA_H,DATA_D)];
			beta[15] = Beta_Volumes[Calculate4DIndex(x,y,z,15,DATA_W,DATA_H,DATA_D)];
			beta[16] = Beta_Volumes[Calculate4DIndex(x,y,z,16,DATA_W,DATA_H,DATA_D)];
			beta[17] = Beta_Volumes[Calculate4DIndex(x,y,z,17,DATA_W,DATA_H,DATA_D)];
			beta[18] = Beta_Volumes[Calculate4DIndex(x,y,z,18,DATA_W,DATA_H,DATA_D)];
			beta[19] = Beta_Volumes[Calculate4DIndex(x,y,z,19,DATA_W,DATA_H,DATA_D)];
			beta[20] = Beta_Volumes[Calculate4DIndex(x,y,z,20,DATA_W,DATA_H,DATA_D)];
			beta[21] = Beta_Volumes[Calculate4DIndex(x,y,z,21,DATA_W,DATA_H,DATA_D)];
			beta[22] = Beta_Volumes[Calculate4DIndex(x,y,z,22,DATA_W,DATA_H,DATA_D)];
			beta[23] = Beta_Volumes[Calculate4DIndex(x,y,z,23,DATA_W,DATA_H,DATA_D)];
			beta[24] = Beta_Volumes[Calculate4DIndex(x,y,z,24,DATA_W,DATA_H,DATA_D)];

			break;


		default:
			1;
			break;
	}

	return 0;
}

/*
int CalculateCovarianceMatricesFirstLevel(__private float* Cxy,
		                                  __private float* Cyy,
		                                  __private float value1,
		                                  __private float value2,
		                                  __constant float* c_X_GLM,
		                                  int v,
		                                  int NUMBER_OF_VOLUMES,
		                                  int NUMBER_OF_REGRESSORS)
{
	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			Cxy[0][0] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value1;
			Cxy[0][1] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value2;

		break;

		case 2:

			Cxy[0][0] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value1;
			Cxy[0][1] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value2;

			Cxy[1][0] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value1;
			Cxy[1][1] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value2;

			break;

		case 3:

			Cxy[0][0] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value1;
			Cxy[0][1] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value2;

			Cxy[1][0] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value1;
			Cxy[1][1] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value2;

			Cxy[2][0] += c_X_GLM[v + 2 * NUMBER_OF_VOLUMES] * value1;
			Cxy[2][1] += c_X_GLM[v + 2 * NUMBER_OF_VOLUMES] * value2;

			break;

		case 4:

			Cxy[0][0] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value1;
			Cxy[0][1] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value2;

			Cxy[1][0] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value1;
			Cxy[1][1] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value2;

			Cxy[2][0] += c_X_GLM[v + 2 * NUMBER_OF_VOLUMES] * value1;
			Cxy[2][1] += c_X_GLM[v + 2 * NUMBER_OF_VOLUMES] * value2;

			Cxy[3][0] += c_X_GLM[v + 3 * NUMBER_OF_VOLUMES] * value1;
			Cxy[3][1] += c_X_GLM[v + 3 * NUMBER_OF_VOLUMES] * value2;

			break;

		case 5:

			Cxy[0][0] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value1;
			Cxy[0][1] += c_X_GLM[v + 0 * NUMBER_OF_VOLUMES] * value2;

			Cxy[1][0] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value1;
			Cxy[1][1] += c_X_GLM[v + 1 * NUMBER_OF_VOLUMES] * value2;

			Cxy[2][0] += c_X_GLM[v + 2 * NUMBER_OF_VOLUMES] * value1;
			Cxy[2][1] += c_X_GLM[v + 2 * NUMBER_OF_VOLUMES] * value2;

			Cxy[3][0] += c_X_GLM[v + 3 * NUMBER_OF_VOLUMES] * value1;
			Cxy[3][1] += c_X_GLM[v + 3 * NUMBER_OF_VOLUMES] * value2;

			Cxy[4][0] += c_X_GLM[v + 4 * NUMBER_OF_VOLUMES] * value1;
			Cxy[4][1] += c_X_GLM[v + 4 * NUMBER_OF_VOLUMES] * value2;

			break;

		default:
			1;
		break;
	}

	Cyy[0][0] += value1 * value1;
	Cyy[0][1] += value1 * value2;
	Cyy[1][0] += value2 * value1;
	Cyy[1][1] += value2 * value2;

	return 0;
}


int NormalizeCovarianceMatrices(__private float* Cxy, __private float* Cyy, int NUMBER_OF_VOLUMES, int NUMBER_OF_REGRESSORS)
{
	float div = ((float)NUMBER_OF_VOLUMES - 1);

	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			Cxy[0][0] /= div;
			Cxy[0][1] /= div;

		break;

		case 2:

			Cxy[0][0] /= div;
			Cxy[0][1] /= div;

			Cxy[1][0] /= div;
			Cxy[1][1] /= div;

			break;

		case 3:

			Cxy[0][0] /= div;
			Cxy[0][1] /= div;

			Cxy[1][0] /= div;
			Cxy[1][1] /= div;

			Cxy[2][0] /= div;
			Cxy[2][1] /= div;

			break;

		case 4:

			Cxy[0][0] /= div;
			Cxy[0][1] /= div;

			Cxy[1][0] /= div;
			Cxy[1][1] /= div;

			Cxy[2][0] /= div;
			Cxy[2][1] /= div;

			Cxy[3][0] /= div;
			Cxy[3][1] /= div;

			break;

		case 5:

			Cxy[0][0] /= div;
			Cxy[0][1] /= div;

			Cxy[1][0] /= div;
			Cxy[1][1] /= div;

			Cxy[2][0] /= div;
			Cxy[2][1] /= div;

			Cxy[3][0] /= div;
			Cxy[3][1] /= div;

			Cxy[4][0] /= div;
			Cxy[4][1] /= div;

			break;

		default:
			1;
		break;
	}

	Cyy[0][0] /= div;
	Cyy[0][1] /= div;
	Cyy[1][0] /= div;
	Cyy[1][1] /= div;

	return 0;
}
*/

int CalculateBetaWeightsFirstLevel(__private float* beta,
		                 	       __private float value,
		                 	 	   __constant float* c_xtxxt_GLM,
		                 	 	   int v,
		                 	 	   int NUMBER_OF_VOLUMES,
		                 	 	   int NUMBER_OF_REGRESSORS)
{
	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];

			break;

		case 2:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];

			break;

		case 3:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];

			break;

		case 4:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];

			break;

		case 5:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];

			break;

		case 6:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];

			break;

		case 7:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];

			break;

		case 8:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];

			break;

		case 9:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];

			break;

		case 10:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];

			break;

		case 11:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];

			break;

		case 12:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];

			break;

		case 13:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			
			break;

		case 14:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			
			break;

		case 15:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			
			break;

		case 16:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			
			break;

		case 17:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			
			break;

		case 18:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			
			break;

		case 19:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];

			break;

		case 20:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];
			beta[19] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 19 + v];
			
			break;

		case 21:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];
			beta[19] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 19 + v];
			beta[20] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 20 + v];
			
			break;

		case 22:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];
			beta[19] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 19 + v];
			beta[20] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 20 + v];
			beta[21] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 21 + v];

			break;

		case 23:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];
			beta[19] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 19 + v];
			beta[20] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 20 + v];
			beta[21] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 21 + v];
			beta[22] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 22 + v];

			break;

		case 24:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];
			beta[19] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 19 + v];
			beta[20] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 20 + v];
			beta[21] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 21 + v];
			beta[22] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 22 + v];
			beta[23] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 23 + v];
			
			break;

		case 25:

			beta[0] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 0 + v];
			beta[1] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 1 + v];
			beta[2] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 2 + v];
			beta[3] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 3 + v];
			beta[4] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 4 + v];
			beta[5] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 5 + v];
			beta[6] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 6 + v];
			beta[7] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 7 + v];
			beta[8] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 8 + v];
			beta[9] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 9 + v];
			beta[10] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 10 + v];
			beta[11] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 11 + v];
			beta[12] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 12 + v];
			beta[13] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 13 + v];
			beta[14] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 14 + v];
			beta[15] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 15 + v];
			beta[16] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 16 + v];
			beta[17] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 17 + v];
			beta[18] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 18 + v];
			beta[19] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 19 + v];
			beta[20] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 20 + v];
			beta[21] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 21 + v];
			beta[22] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 22 + v];
			beta[23] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 23 + v];
			beta[24] += value * c_xtxxt_GLM[NUMBER_OF_VOLUMES * 24 + v];
			
			break;


		default:
			1;
			break;
	}

	return 0;
}






// For first level, volumes already permuted
float CalculateEpsFirstLevel(__private float eps,
							 __private float* beta,
							 __constant float* c_X_GLM,
							 int v,		           
							 int NUMBER_OF_VOLUMES,
							 int NUMBER_OF_REGRESSORS)
{
	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];

			break;

		case 2:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];

			break;

		case 3:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];

			break;

		case 4:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];

			break;

		case 5:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];

			break;

		case 6:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];

			break;

		case 7:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];

			break;

		case 8:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];

			break;

		case 9:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];

			break;

		case 10:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];			

			break;

		case 11:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			
			break;

		case 12:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			
			break;

		case 13:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			
			break;

		case 14:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			
			break;

		case 15:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			
			break;

		case 16:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			
			break;

		case 17:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			
			break;

		case 18:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			
			break;

		case 19:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			
			break;

		case 20:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 19 + v] * beta[19];
			
			break;

		case 21:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 19 + v] * beta[19];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 20 + v] * beta[20];
			
			break;

		case 22:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 19 + v] * beta[19];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 20 + v] * beta[20];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 21 + v] * beta[21];
			
			break;

		case 23:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 19 + v] * beta[19];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 20 + v] * beta[20];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 21 + v] * beta[21];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 22 + v] * beta[22];
			
			break;

		case 24:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 19 + v] * beta[19];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 20 + v] * beta[20];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 21 + v] * beta[21];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 22 + v] * beta[22];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 23 + v] * beta[23];
			
			break;

		case 25:

			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 0 + v] * beta[0];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 1 + v] * beta[1];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 2 + v] * beta[2];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 3 + v] * beta[3];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 4 + v] * beta[4];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 5 + v] * beta[5];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 6 + v] * beta[6];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 7 + v] * beta[7];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 8 + v] * beta[8];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 9 + v] * beta[9];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 10 + v] * beta[10];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 11 + v] * beta[11];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 12 + v] * beta[12];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 13 + v] * beta[13];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 14 + v] * beta[14];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 15 + v] * beta[15];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 16 + v] * beta[16];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 17 + v] * beta[17];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 18 + v] * beta[18];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 19 + v] * beta[19];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 20 + v] * beta[20];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 21 + v] * beta[21];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 22 + v] * beta[22];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 23 + v] * beta[23];
			eps -= c_X_GLM[NUMBER_OF_VOLUMES * 24 + v] * beta[24];

			break;

		default:
			1;
			break;
	}

	return eps;
}




float CalculateContrastValue(__private float* beta, __constant float* c_Contrasts, int c, int NUMBER_OF_REGRESSORS)
{
	float contrast_value = 0.0f;

	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];

			break;

		case 2:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];

			break;

		case 3:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];

			break;

		case 4:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];

			break;

		case 5:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];

			break;

		case 6:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];

			break;

		case 7:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];

			break;

		case 8:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];

			break;

		case 9:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];

			break;

		case 10:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];

			break;

		case 11:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			
			break;

		case 12:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			
			break;

		case 13:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			
			break;

		case 14:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			
			break;

		case 15:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			
			break;

		case 16:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			
			break;

		case 17:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			
			break;

		case 18:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			
			break;

		case 19:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			
			break;

		case 20:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			
			break;

		case 21:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];

			break;

		case 22:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			
			break;

		case 23:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 22] * beta[22];
			
			break;

		case 24:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 22] * beta[22];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 23] * beta[23];
			
			break;

		case 25:

			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 22] * beta[22];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 23] * beta[23];
			contrast_value += c_Contrasts[NUMBER_OF_REGRESSORS * c + 24] * beta[24];

			break;


		default:
			1;
			break;
	}

	return contrast_value;
}



int CalculateCBeta(__private float* cbeta, __private float* beta, __constant float* c_Contrasts, int c, int NUMBER_OF_REGRESSORS)	
{
	cbeta[c] = 0.0f;

	switch(NUMBER_OF_REGRESSORS)
	{
		case 1:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];

			break;

		case 2:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			
			break;

		case 3:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			
			break;

		case 4:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			
			break;

		case 5:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			
			break;

		case 6:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			
			break;

		case 7:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			
			break;

		case 8:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			
			break;

		case 9:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			
			break;

		case 10:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			
			break;

		case 11:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			
			break;

		case 12:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			
			break;

		case 13:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			
			break;

		case 14:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			
			break;

		case 15:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			
			break;

		case 16:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			
			break;

		case 17:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			
			break;

		case 18:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			
			break;

		case 19:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			
			break;

		case 20:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			
			break;

		case 21:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			
			break;

		case 22:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			
			break;

		case 23:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 22] * beta[22];
			
			break;

		case 24:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 22] * beta[22];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 23] * beta[23];
			
			break;

		case 25:

			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 0] * beta[0];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 1] * beta[1];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 2] * beta[2];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 3] * beta[3];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 4] * beta[4];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 5] * beta[5];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 6] * beta[6];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 7] * beta[7];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 8] * beta[8];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 9] * beta[9];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 10] * beta[10];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 11] * beta[11];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 12] * beta[12];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 13] * beta[13];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 14] * beta[14];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 15] * beta[15];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 16] * beta[16];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 17] * beta[17];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 18] * beta[18];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 19] * beta[19];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 20] * beta[20];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 21] * beta[21];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 22] * beta[22];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 23] * beta[23];
			cbeta[c] += c_Contrasts[NUMBER_OF_REGRESSORS * c + 24] * beta[24];			
			
			break;

		default:
			1;
			break;
	}

	return 0;
}

int CalculateCBetas(__private float* cbeta, __private float* beta, __constant float* c_Contrasts, int NUMBER_OF_REGRESSORS, int NUMBER_OF_CONTRASTS)
{
	switch(NUMBER_OF_CONTRASTS)
	{
		case 1:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);

			break;

		case 2:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);

			break;

		case 3:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			
			break;

		case 4:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			
			break;

		case 5:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 4, NUMBER_OF_REGRESSORS);

			break;

		case 6:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 4, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 5, NUMBER_OF_REGRESSORS);

			break;

		case 7:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 4, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 5, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 6, NUMBER_OF_REGRESSORS);

			break;

		case 8:
			
			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 4, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 5, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 6, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 7, NUMBER_OF_REGRESSORS);

			break;

		case 9:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 4, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 5, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 6, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 7, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 8, NUMBER_OF_REGRESSORS);

			break;

		case 10:

			CalculateCBeta(cbeta, beta, c_Contrasts, 0, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 1, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 2, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 3, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 4, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 5, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 6, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 7, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 8, NUMBER_OF_REGRESSORS);
			CalculateCBeta(cbeta, beta, c_Contrasts, 9, NUMBER_OF_REGRESSORS);
			
			break;		

		default:
			1;
			break;
	}	

	return 0;
}

int CalculateCTXTXCCBeta(__private float* beta, float vareps, __constant float* c_ctxtxc_GLM, __private float* cbeta, int c,  int NUMBER_OF_CONTRASTS)
{
	beta[c] = 0.0f;

	switch(NUMBER_OF_CONTRASTS)
	{
		case 1:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			
			break;

		case 2:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			
			break;

		case 3:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			
			break;

		case 4:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			
			break;

		case 5:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[4 + c * NUMBER_OF_CONTRASTS] * cbeta[4];
			
			break;

		case 6:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[4 + c * NUMBER_OF_CONTRASTS] * cbeta[4];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[5 + c * NUMBER_OF_CONTRASTS] * cbeta[5];
			
			break;

		case 7:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[4 + c * NUMBER_OF_CONTRASTS] * cbeta[4];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[5 + c * NUMBER_OF_CONTRASTS] * cbeta[5];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[6 + c * NUMBER_OF_CONTRASTS] * cbeta[6];
			
			break;

		case 8:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[4 + c * NUMBER_OF_CONTRASTS] * cbeta[4];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[5 + c * NUMBER_OF_CONTRASTS] * cbeta[5];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[6 + c * NUMBER_OF_CONTRASTS] * cbeta[6];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[7 + c * NUMBER_OF_CONTRASTS] * cbeta[7];
			
			break;

		case 9:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[4 + c * NUMBER_OF_CONTRASTS] * cbeta[4];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[5 + c * NUMBER_OF_CONTRASTS] * cbeta[5];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[6 + c * NUMBER_OF_CONTRASTS] * cbeta[6];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[7 + c * NUMBER_OF_CONTRASTS] * cbeta[7];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[8 + c * NUMBER_OF_CONTRASTS] * cbeta[8];
			
			break;

		case 10:

			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[0 + c * NUMBER_OF_CONTRASTS] * cbeta[0];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[1 + c * NUMBER_OF_CONTRASTS] * cbeta[1];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[2 + c * NUMBER_OF_CONTRASTS] * cbeta[2];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[3 + c * NUMBER_OF_CONTRASTS] * cbeta[3];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[4 + c * NUMBER_OF_CONTRASTS] * cbeta[4];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[5 + c * NUMBER_OF_CONTRASTS] * cbeta[5];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[6 + c * NUMBER_OF_CONTRASTS] * cbeta[6];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[7 + c * NUMBER_OF_CONTRASTS] * cbeta[7];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[8 + c * NUMBER_OF_CONTRASTS] * cbeta[8];
			beta[c] += 1.0f/vareps * c_ctxtxc_GLM[9 + c * NUMBER_OF_CONTRASTS] * cbeta[9];

			break;		

		default:
			1;
			break;
	}	

	return 0;	
}			


int CalculateCTXTXCCBetas(__private float* beta, float vareps, __constant float* c_ctxtxc_GLM, __private float* cbeta, int NUMBER_OF_CONTRASTS)
{
	switch(NUMBER_OF_CONTRASTS)
	{
		case 1:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			
			break;

		case 2:
			
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
						
			break;

		case 3:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);

			break;

		case 4:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			
			break;

		case 5:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 4, NUMBER_OF_CONTRASTS);

			break;

		case 6:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 4, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 5, NUMBER_OF_CONTRASTS);
			
			break;

		case 7:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 4, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 5, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 6, NUMBER_OF_CONTRASTS);
			
			break;

		case 8:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 4, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 5, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 6, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 7, NUMBER_OF_CONTRASTS);

			break;

		case 9:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 4, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 5, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 6, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 7, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 8, NUMBER_OF_CONTRASTS);

			break;

		case 10:

			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 0, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 1, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 2, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 3, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 4, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 5, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 6, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 7, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 8, NUMBER_OF_CONTRASTS);
			CalculateCTXTXCCBeta(beta, vareps, c_ctxtxc_GLM, cbeta, 9, NUMBER_OF_CONTRASTS);
			
			break;		

		default:
			1;
			break;
	}	

	return 0;	
}			


float CalculateFTestScalar(__private float* cbeta, __private float* beta, int NUMBER_OF_CONTRASTS)
{
	float scalar = 0.0f;

	switch(NUMBER_OF_CONTRASTS)
	{
		case 1:

			scalar += cbeta[0] * beta[0];
			
			break;

		case 2:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			
			break;

		case 3:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			
			break;

		case 4:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			
			break;

		case 5:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			scalar += cbeta[4] * beta[4];
			
			break;

		case 6:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			scalar += cbeta[4] * beta[4];
			scalar += cbeta[5] * beta[5];
			
			break;

		case 7:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			scalar += cbeta[4] * beta[4];
			scalar += cbeta[5] * beta[5];
			scalar += cbeta[6] * beta[6];
			scalar += cbeta[7] * beta[7];
			
			break;

		case 8:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			scalar += cbeta[4] * beta[4];
			scalar += cbeta[5] * beta[5];
			scalar += cbeta[6] * beta[6];
			scalar += cbeta[7] * beta[7];
			
			break;

		case 9:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			scalar += cbeta[4] * beta[4];
			scalar += cbeta[5] * beta[5];
			scalar += cbeta[6] * beta[6];
			scalar += cbeta[7] * beta[7];
			scalar += cbeta[8] * beta[8];
			
			break;

		case 10:

			scalar += cbeta[0] * beta[0];
			scalar += cbeta[1] * beta[1];
			scalar += cbeta[2] * beta[2];
			scalar += cbeta[3] * beta[3];
			scalar += cbeta[4] * beta[4];
			scalar += cbeta[5] * beta[5];
			scalar += cbeta[6] * beta[6];
			scalar += cbeta[7] * beta[7];
			scalar += cbeta[8] * beta[8];
			scalar += cbeta[9] * beta[9];
			
			break;		

		default:
			1;
			break;
	}	

	return scalar;
}

	



__kernel void CalculateStatisticalMapsGLMTTestFirstLevelPermutation(__global float* Statistical_Maps,
																	__global const float* Volumes,
																	__global const float* Mask,
																	__constant float* c_X_GLM,
																	__constant float* c_xtxxt_GLM,
																	__constant float* c_Contrasts,	
																	__constant float* c_ctxtxc_GLM,
																	__private int DATA_W,
																	__private int DATA_H,
																	__private int DATA_D,
																	__private int NUMBER_OF_VOLUMES,
																	__private int NUMBER_OF_REGRESSORS,
																	__private int NUMBER_OF_CONTRASTS,
																	__private int contrast)
{	
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
		return;

	int t = 0;
	float eps, meaneps, vareps;
	float beta[25];

	// Reset beta weights
	beta[0] = 0.0f;
	beta[1] = 0.0f;
	beta[2] = 0.0f;
	beta[3] = 0.0f;
	beta[4] = 0.0f;
	beta[5] = 0.0f;
	beta[6] = 0.0f;
	beta[7] = 0.0f;
	beta[8] = 0.0f;
	beta[9] = 0.0f;
	beta[10] = 0.0f;
	beta[11] = 0.0f;
	beta[12] = 0.0f;
	beta[13] = 0.0f;
	beta[14] = 0.0f;
	beta[15] = 0.0f;
	beta[16] = 0.0f;
	beta[17] = 0.0f;
	beta[18] = 0.0f;
	beta[19] = 0.0f;
	beta[20] = 0.0f;
	beta[21] = 0.0f;
	beta[22] = 0.0f;
	beta[23] = 0.0f;
	beta[24] = 0.0f;

	// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
	// Loop over volumes
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		float value = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

		// Loop over regressors using unrolled code for performance
		CalculateBetaWeightsFirstLevel(beta, value, c_xtxxt_GLM, v, NUMBER_OF_VOLUMES, NUMBER_OF_REGRESSORS);
	}

	// Calculate the mean and variance of the error eps
	meaneps = 0.0f;
	vareps = 0.0f;
	float n = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		eps = CalculateEpsFirstLevel(eps, beta, c_X_GLM, v, NUMBER_OF_VOLUMES, NUMBER_OF_REGRESSORS);
		
		n += 1.0f;
		float delta = eps - meaneps;
		meaneps += delta/n;
		vareps += delta * (eps - meaneps);
	}
	vareps = vareps / (n - 1.0f);

	// Calculate t-values
	float contrast_value = CalculateContrastValue(beta, c_Contrasts, contrast, NUMBER_OF_REGRESSORS);
	Statistical_Maps[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = contrast_value * rsqrt(vareps * c_ctxtxc_GLM[contrast]);	
}


__kernel void CalculateStatisticalMapsGLMFTestFirstLevelPermutation(__global float* Statistical_Maps,
					 		                                        __global const float* Volumes,
																	__global const float* Mask,
																	__constant float* c_X_GLM,
																	__constant float* c_xtxxt_GLM,
																	__constant float* c_Contrasts,
																	__constant float* c_ctxtxc_GLM,
																	__private int DATA_W,
																	__private int DATA_H,
																	__private int DATA_D,
																	__private int NUMBER_OF_VOLUMES,
																	__private int NUMBER_OF_REGRESSORS,
																	__private int NUMBER_OF_CONTRASTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
		return;

	int t = 0;
	float eps, meaneps, vareps;
	float beta[25];

	// Reset beta weights
	beta[0] = 0.0f;
	beta[1] = 0.0f;
	beta[2] = 0.0f;
	beta[3] = 0.0f;
	beta[4] = 0.0f;
	beta[5] = 0.0f;
	beta[6] = 0.0f;
	beta[7] = 0.0f;
	beta[8] = 0.0f;
	beta[9] = 0.0f;
	beta[10] = 0.0f;
	beta[11] = 0.0f;
	beta[12] = 0.0f;
	beta[13] = 0.0f;
	beta[14] = 0.0f;
	beta[15] = 0.0f;
	beta[16] = 0.0f;
	beta[17] = 0.0f;
	beta[18] = 0.0f;
	beta[19] = 0.0f;
	beta[20] = 0.0f;
	beta[21] = 0.0f;
	beta[22] = 0.0f;
	beta[23] = 0.0f;
	beta[24] = 0.0f;

	// Calculate betahat, i.e. multiply (x^T x)^(-1) x^T with Y
	// Loop over volumes
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		float value = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

		// Loop over regressors using unrolled code for performance
		CalculateBetaWeightsFirstLevel(beta, value, c_xtxxt_GLM, v, NUMBER_OF_VOLUMES, NUMBER_OF_REGRESSORS);
	}

	// Calculate the mean and variance of the error eps
	meaneps = 0.0f;
	vareps = 0.0f;
	float n = 0.0f;
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		eps = CalculateEpsFirstLevel(eps, beta, c_X_GLM, v, NUMBER_OF_VOLUMES, NUMBER_OF_REGRESSORS);
		
		n += 1.0f;
		float delta = eps - meaneps;
		meaneps += delta/n;
		vareps += delta * (eps - meaneps);
	}
	vareps = vareps / (n - 1.0f);

	//-------------------------

	// Calculate matrix vector product C*beta (minus u)
	float cbeta[10];
	CalculateCBetas(cbeta, beta, c_Contrasts, NUMBER_OF_REGRESSORS, NUMBER_OF_CONTRASTS);		

	// Calculate total vector matrix vector product (C*beta)^T ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)

	// Calculate right hand side, temp = ( 1/vareps * (C^T (X^T X)^(-1) C^T)^(-1) ) (C*beta)	
	CalculateCTXTXCCBetas(beta, vareps, c_ctxtxc_GLM, cbeta, NUMBER_OF_CONTRASTS);

	// Finally calculate (C*beta)^T * temp
	float scalar = CalculateFTestScalar(cbeta,beta,NUMBER_OF_CONTRASTS);

	// Save F-value
	Statistical_Maps[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] = scalar/(float)NUMBER_OF_CONTRASTS;
}

/*
__kernel void CalculateStatisticalMapsCCAFirstLevelPermutation(__global float* Statistical_Maps,
														 	   __global const float* Volumes1,
														 	   __global const float* Volumes2,
															   __global const float* Mask,
															   __constant float* c_X_GLM,
															   __constant float* c_xtxxt_GLM,
															   __constant float* c_Contrasts,
															   __constant float* c_ctxtxc_GLM,
															   __private int DATA_W,
															   __private int DATA_H,
															   __private int DATA_D,
															   __private int NUMBER_OF_VOLUMES,
															   __private int NUMBER_OF_REGRESSORS,
															   __private int NUMBER_OF_CONTRASTS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
		return;

	float eps, meaneps, vareps;
	float beta[25];

	// Reset beta weights
	beta[0] = 0.0f;
	beta[1] = 0.0f;
	beta[2] = 0.0f;
	beta[3] = 0.0f;
	beta[4] = 0.0f;
	beta[5] = 0.0f;
	beta[6] = 0.0f;
	beta[7] = 0.0f;
	beta[8] = 0.0f;
	beta[9] = 0.0f;
	beta[10] = 0.0f;
	beta[11] = 0.0f;
	beta[12] = 0.0f;
	beta[13] = 0.0f;
	beta[14] = 0.0f;
	beta[15] = 0.0f;
	beta[16] = 0.0f;
	beta[17] = 0.0f;
	beta[18] = 0.0f;
	beta[19] = 0.0f;
	beta[20] = 0.0f;
	beta[21] = 0.0f;
	beta[22] = 0.0f;
	beta[23] = 0.0f;
	beta[24] = 0.0f;

	float Cxy[5][2];
	float Cyy[2][2];
	float inv_Cyy[2][2];


	// Calculate the covariance matrices, Cxx is precalculated and stored in constant memory
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		float value1 = Volumes1[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		float value2 = Volumes2[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];

		// Loop over regressors using unrolled code for performance
		CalculateCovarianceMatricesFirstLevel(Cxy, Cyy, value1, value2, c_X_GLM, v, NUMBER_OF_VOLUMES, NUMBER_OF_REGRESSORS);
	}

	NormalizeCovarianceMatrices(Cxy, Cyy, NUMBER_OF_VOLUMES, NUMBER_OF_REGRESSORS);

	// Invert Cyy covariance matrix
	Invert_2x2(Cyy, inv_Cyy);
	// Calculate square root of inverseInvert Cyy covariance matrix
	MatrixSqrt_2x2(inv_Cyy, Cyy);


	// Calculate the total matrix product, gives a 2 x 2 matrix,  (Cyy)^(-1/2) * Cyx * (Cxx)^(-1) * Cxy * (Cyy)^(-1/2)
	// First step, calculate Cyx * (Cxx)^(-1) * Cxy, three values sufficient since matrix is symmetric


}
*/

// Optimized kernel for calculating t-test values for permutations, second level




__kernel void TransformData(__global float* Transformed_Volumes,
							__global float* Volumes,
		                    __global const float* Mask,
   	   	   				    __constant float* c_X,
		                    __private int DATA_W,
		                    __private int DATA_H,
		                    __private int DATA_D,
		                    __private int NUMBER_OF_VOLUMES)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	int3 tIdx = {get_local_id(0), get_local_id(1), get_local_id(2)};

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
		return;

	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		Transformed_Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;

		for (int vv = 0; vv < NUMBER_OF_VOLUMES; vv++)
		{
			Transformed_Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] += c_X[vv + v * NUMBER_OF_VOLUMES] * Volumes[Calculate4DIndex(x,y,z,vv,DATA_W,DATA_H,DATA_D)];
		}
	}

	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = Transformed_Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
	}
}




// Removes a linear fit estimated with CalculateGLMBetaWeights
__kernel void RemoveLinearFit(__global float* Residual_Volumes, 
                              __global const float* Volumes, 
							  __global const float* Beta_Volumes, 
							  __global const float* Mask, 
							  __constant float *c_X_Detrend, 
							  __private int DATA_W, 
							  __private int DATA_H, 
							  __private int DATA_D, 
							  __private int NUMBER_OF_VOLUMES, 
							  __private int NUMBER_OF_REGRESSORS)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,z,DATA_W,DATA_H)] != 1.0f )
	{
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residual_Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = 0.0f;
		}

		return;
	}
	
	float eps;
	float beta[100];

	// Load beta values into regressors
    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{ 
		beta[r] = Beta_Volumes[Calculate4DIndex(x,y,z,r,DATA_W,DATA_H,DATA_D)];
	}

	// Calculate the residual
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{ 			
			eps -= beta[r] * c_X_Detrend[NUMBER_OF_VOLUMES * r + v];
		}
		Residual_Volumes[Calculate4DIndex(x,y,z,v,DATA_W,DATA_H,DATA_D)] = eps;		
	}
}


// Removes a linear fit estimated with CalculateGLMBetaWeights, for one slice
__kernel void RemoveLinearFitSlice(__global float* Residual_Volumes, 
		                           __global const float* Volumes, 
								   __global const float* Beta_Volumes, 
							  	   __global const float* Mask, 
							  	   __constant float *c_X_Detrend, 
							  	   __private int DATA_W, 
							  	   __private int DATA_H, 
							  	   __private int DATA_D, 
							  	   __private int NUMBER_OF_VOLUMES, 
							  	   __private int NUMBER_OF_REGRESSORS,
							  	   __private int slice)
{
	int x = get_global_id(0);
	int y = get_global_id(1);
	int z = get_global_id(2);

	if (x >= DATA_W || y >= DATA_H || z >= DATA_D)
		return;

	if ( Mask[Calculate3DIndex(x,y,slice,DATA_W,DATA_H)] != 1.0f )
	{
		for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
		{
			Residual_Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = 0.0f;
		}

		return;
	}
	
	float eps;
	float beta[100];

	// Load beta values into regressors
    for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
	{ 
		beta[r] = Beta_Volumes[Calculate4DIndex(x,y,slice,r,DATA_W,DATA_H,DATA_D)];
	}

	// Calculate the residual
	for (int v = 0; v < NUMBER_OF_VOLUMES; v++)
	{
		eps = Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)];
		for (int r = 0; r < NUMBER_OF_REGRESSORS; r++)
		{ 			
			eps -= beta[r] * c_X_Detrend[NUMBER_OF_VOLUMES * r + v];
		}
		Residual_Volumes[Calculate3DIndex(x,y,v,DATA_W,DATA_H)] = eps;
	}
}




