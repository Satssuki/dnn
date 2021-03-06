/** -*- c++ -*- 
 *
 * \file   conv_layer.cpp
 * \date   Sat May 14 11:30:53 2016
 *
 * \copyright 
 * Copyright (c) 2016 Liangfu Chen <liangfu.chen@nlpr.ia.ac.cn>.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the Brainnetome Center & NLPR at Institute of Automation, CAS. The 
 * name of the Brainnetome Center & NLPR at Institute of Automation, CAS 
 * may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * \brief  Convolutional Neural Network (ConvNet,CNN) layer
 */

#include "_dnn.h"

void icvCNNConvolutionForwardDirect( CvDNNLayer* _layer, const CvMat* X, CvMat* Y );
void icvCNNConvolutionForwardFFT( CvDNNLayer* _layer, const CvMat* X, CvMat* Y );

/*************************************************************************/
ML_IMPL CvDNNLayer* cvCreateConvolutionLayer( 
    const int dtype, const char * name, const CvDNNLayer * ref_layer,
    const int visualize, const CvDNNLayer * input_layer, 
    int n_input_planes, int input_height, int input_width, int n_output_planes, int K,
    float init_learn_rate, int update_rule, const char * activation,
    CvMat* connect_mask, CvMat* weights )

{
  CvDNNConvolutionLayer* layer = 0;

  CV_FUNCNAME("cvCreateConvolutionLayer");
  __BEGIN__;

  const int output_height = input_height - K + 1;
  const int output_width = input_width - K + 1;
  fprintf(stderr,"ConvolutionLayer(%s): input (%d@%dx%d), output (%d@%dx%d)\n", name,
          n_input_planes,input_width,input_height,
          n_output_planes,output_width,output_height);

  if ( K < 1 || init_learn_rate <= 0 || init_learn_rate > 1 ) {
    CV_ERROR( CV_StsBadArg, "Incorrect parameters" );
  }

  CV_CALL(layer = (CvDNNConvolutionLayer*)icvCreateLayer( 
    ICV_DNN_CONVOLUTION_LAYER, dtype, name, sizeof(CvDNNConvolutionLayer), 
    n_input_planes, input_height, input_width,
    n_output_planes, output_height, output_width,
    init_learn_rate, update_rule, 
    icvCNNConvolutionRelease, icvCNNConvolutionForward, icvCNNConvolutionBackward ));

  strcpy(layer->activation,activation);
  layer->enable_cache = 1;
  layer->K = K;
  layer->seq_length = 1;
  layer->visualize = visualize;
  layer->ref_layer = (CvDNNLayer*)ref_layer;
  if (input_layer){layer->input_layers.push_back((CvDNNLayer*)input_layer);}
  CV_CALL(layer->weights = cvCreateMat( n_output_planes, K*K+1, CV_32FC1 ));
  CV_CALL(layer->connect_mask = cvCreateMat( n_output_planes, n_input_planes, CV_8UC1));

  if ( weights ){
    if ( !ICV_IS_MAT_OF_TYPE( weights, CV_32FC1 ) ) {
      CV_ERROR( CV_StsBadSize, "Type of initial weights matrix must be CV_32FC1" );
    }
    if ( !CV_ARE_SIZES_EQ( weights, layer->weights ) ) {
      CV_ERROR( CV_StsBadSize, "Invalid size of initial weights matrix" );
    }
    CV_CALL(cvCopy( weights, layer->weights ));
  }else{
    CvRNG rng = cvRNG( -1 ); float invKK = 1./float(K*K);
    cvRandArr( &rng, layer->weights, CV_RAND_UNI, cvScalar(-1), cvScalar(1) );
    // normalize weights
    CvMat * sum = cvCreateMat(n_output_planes,1,CV_32F);
    CvMat * sumrep = cvCreateMat(n_output_planes,layer->weights->cols,CV_32F);
    cvReduce(layer->weights,sum,-1,CV_REDUCE_SUM); cvScale(sum,sum,invKK);
    cvRepeat(sum,sumrep);
    cvSub(layer->weights,sumrep,layer->weights);
    cvReleaseMat(&sum);
    cvReleaseMat(&sumrep);
    // initialize bias to zero
    for (int ii=0;ii<layer->weights->rows;ii++){ CV_MAT_ELEM(*layer->weights,float,ii,K*K)=0; }
  }

  if ( connect_mask ) {
    if ( !ICV_IS_MAT_OF_TYPE( connect_mask, CV_8UC1 ) ) {
      CV_ERROR( CV_StsBadSize, "Type of connection matrix must be CV_32FC1" );
    }
    if ( !CV_ARE_SIZES_EQ( connect_mask, layer->connect_mask ) ) {
      CV_ERROR( CV_StsBadSize, "Invalid size of connection matrix" );
    }
    CV_CALL(cvCopy( connect_mask, layer->connect_mask ));
  }else{
    CV_CALL(cvSet( layer->connect_mask, cvRealScalar(1) ));
  }

  __END__;

  if ( cvGetErrStatus() < 0 && layer ){
    cvReleaseMat( &layer->weights );
    cvReleaseMat( &layer->connect_mask );
    cvFree( &layer );
  }

  return (CvDNNLayer*)layer;
}

void icvCNNConvolutionForward( CvDNNLayer* _layer, const CvMat* X, CvMat* Y )
{
#if 1
  icvCNNConvolutionForwardDirect(_layer, X, Y);
#else
  icvCNNConvolutionForwardFFT(_layer, X, Y);
#endif
}

void icvCNNConvolutionForwardDirect( CvDNNLayer* _layer, const CvMat* X, CvMat* Y )
{
  CV_FUNCNAME("icvCNNConvolutionForwardDirect");

  if (!icvIsConvolutionLayer(_layer)){CV_ERROR( CV_StsBadArg, "Invalid layer" );}

  __BEGIN__;

  CvDNNConvolutionLayer* layer = (CvDNNConvolutionLayer*) _layer;
  CvDNNLayer * ref_layer = layer->ref_layer;
  CvMat * weights = ref_layer?ref_layer->weights:layer->weights;
  
  const int K = layer->K;
  const int n_weights_for_Yplane = K*K + 1;
  CV_ASSERT(weights->cols==n_weights_for_Yplane);

  const int nXplanes = layer->n_input_planes;
  const int Xheight  = layer->input_height;
  const int Xwidth   = layer->input_width ;
  const int Xsize    = Xwidth*Xheight;

  const int nYplanes = layer->n_output_planes;
  const int Yheight  = layer->output_height;
  const int Ywidth   = layer->output_width;
  const int Ysize    = Ywidth*Yheight;

  const int nsamples = X->rows; // training batch size

  // int no; xx, yy, ni, , kx, ky
  // float *Yplane = 0, *Xplane = 0, *w = 0;
  uchar* connect_mask_data = 0;

  CV_ASSERT( X->cols == nXplanes*Xsize && X->rows == nsamples );
  CV_ASSERT( Y->cols == nYplanes*Ysize && Y->rows == nsamples );
  CV_ASSERT( Xheight-K+1 == Yheight && Xwidth-K+1 == Ywidth );

  cvSetZero( Y );

  // Yplane = Y->data.fl;
  // w = layer->weights->data.fl;
  connect_mask_data = layer->connect_mask->data.ptr;

  // normalize input
  CvScalar avg,sdv;
  for ( int si = 0; si < nsamples; si++ ){
  for ( int no = 0; no < nXplanes; no++ ){
    float * xptr = X->data.fl+Xsize*nXplanes*si+Xsize*no;
    CvMat img = cvMat(Xsize,1,CV_32F,xptr);
    cvAvgSdv(&img,&avg,&sdv);
    cvSubS(&img,avg,&img);
    cvScale(&img,&img,.5f/(1e-5f+sdv.val[0]));
  }
  }
  
  // for ( no = 0; no < nYplanes; no++, Yplane += Ysize, w += n_weights_for_Yplane ){
#pragma omp parallel for
  for ( int si = 0; si < nsamples; si++ ){
    for ( int no = 0; no < nYplanes; no++ ){
    float * xptr = X->data.fl+Xsize*nXplanes*si;
    float * yptr = Y->data.fl+Ysize*nYplanes*si+Ysize*no;
    float * wptr = weights->data.fl+n_weights_for_Yplane*no;
    for ( int ni = 0; ni < nXplanes; ni++, xptr += Xsize ){
      for ( int yy = 0; yy < Yheight; yy++ ){
      for ( int xx = 0; xx < Ywidth; xx++ ){
        float WX = 0; int xrstep=Xwidth*yy+xx;
        for ( int ky = 0; ky < K; ky++ ){
        int xcstep=Xwidth*ky,wstep=K*ky;
        for ( int kx=0; kx < K; kx++ ){
          WX += (xptr+xrstep)[xcstep+kx]*wptr[wstep+kx];
        } // kx
        } // ky
        yptr[Ywidth*yy+xx] += WX + wptr[K*K]; // bias
      } // xx
      } // yy
    } // ni
    } // no
  } // si

  cvScale(Y,Y,1.f/float(K*K)); //fprintf(stderr,"avg: %f, sdv: %f\n",cvAvg(Y).val[0],cvSdv(Y));
  if (!layer->WX){layer->WX=cvCloneMat(Y);}
  else if (layer->WX->rows==Y->rows){cvCopy(Y,layer->WX);}
  else{cvReleaseMat(&layer->WX);layer->WX=cvCloneMat(Y);}

  if (!strcmp(layer->activation,"none")){ // do nothing
  }else if (!strcmp(layer->activation,"tanh")){ CV_CALL(cvTanh( Y, Y ));
  }else if (!strcmp(layer->activation,"sigmoid")){ CV_CALL(cvSigmoid( Y, Y ));
  }else if (!strcmp(layer->activation,"relu")){ CV_CALL(cvReLU( Y, Y ));
  }else{CV_ERROR(CV_StsBadArg,"Unknown activation type");}

  CV_ASSERT(cvCountNAN(Y)<1);
  
  if (layer->Y){
    if (layer->Y->rows==Y->rows){cvCopy(Y,layer->Y);}else{cvReleaseMat(&layer->Y);layer->Y=cvCloneMat(Y);}
  }else{layer->Y=cvCloneMat(Y);}
  if (layer->visualize){icvVisualizeCNNLayer((CvDNNLayer*)layer,Y);}

  __END__;
}

void icvCNNConvolutionForwardFFT( CvDNNLayer* _layer, const CvMat* X, CvMat* Y )
{
  CV_FUNCNAME("icvCNNConvolutionForwardFFT");

  if (!icvIsConvolutionLayer(_layer)){CV_ERROR( CV_StsBadArg, "Invalid layer" );}

  __BEGIN__;

  CvDNNConvolutionLayer* layer = (CvDNNConvolutionLayer*) _layer;
  CvDNNLayer * ref_layer = layer->ref_layer;
  CvMat * weights = ref_layer?ref_layer->weights:layer->weights;
  
  const int K = layer->K;
  const int n_weights_for_Yplane = K*K + 1;
  CV_ASSERT(weights->cols==n_weights_for_Yplane);

  const int nXplanes = layer->n_input_planes;
  const int Xheight  = layer->input_height;
  const int Xwidth   = layer->input_width ;
  const int Xsize    = Xwidth*Xheight;

  const int nYplanes = layer->n_output_planes;
  const int Yheight  = layer->output_height;
  const int Ywidth   = layer->output_width;
  const int Ysize    = Ywidth*Yheight;

  const int nsamples = X->rows; // training batch size

  // int no; xx, yy, ni, , kx, ky
  // float *Yplane = 0, *Xplane = 0, *w = 0;
  uchar* connect_mask_data = 0;

  CV_ASSERT( X->cols == nXplanes*Xsize && X->rows == nsamples );
  CV_ASSERT( Y->cols == nYplanes*Ysize && Y->rows == nsamples );
  CV_ASSERT( Xheight-K+1 == Yheight && Xwidth-K+1 == Ywidth );

  cvSetZero( Y );

  // Yplane = Y->data.fl;
  // w = layer->weights->data.fl;
  connect_mask_data = layer->connect_mask->data.ptr;

  // normalize input
  CvScalar avg,sdv;
  for ( int si = 0; si < nsamples; si++ ){
  for ( int no = 0; no < nXplanes; no++ ){
    float * xptr = X->data.fl+Xsize*nXplanes*si+Xsize*no;
    CvMat img = cvMat(Xsize,1,CV_32F,xptr);
    cvAvgSdv(&img,&avg,&sdv);
    cvSubS(&img,avg,&img);
    cvScale(&img,&img,.5f/(1e-5f+sdv.val[0]));
  }
  }
  
  const int dft_M = cvGetOptimalDFTSize(Xheight+K-1);
  const int dft_N = cvGetOptimalDFTSize(Xwidth+K-1);
#pragma omp parallel for
  for ( int si = 0; si < nsamples; si++ ){
    CvMat * dft_A = cvCreateMat(dft_M, dft_N, CV_32F); cvZero(dft_A);
    CvMat * dft_B = cvCreateMat(dft_M, dft_N, CV_32F); cvZero(dft_B);
    for ( int no = 0; no < nYplanes; no++ ){
    float * xptr = X->data.fl+Xsize*nXplanes*si;
    float * yptr = Y->data.fl+Ysize*nYplanes*si+Ysize*no;
    float * wptr = weights->data.fl+n_weights_for_Yplane*no;
    CvMat submat_hdr;
    CvMat B = cvMat(K,K,CV_32F,wptr);
    cvGetSubRect( dft_B, &submat_hdr, cvRect(0,0,B.cols,B.rows)); cvCopy(&B,&submat_hdr);
    cvFlip(&submat_hdr,&submat_hdr,-1);
    cvGetSubRect( dft_B, &submat_hdr, cvRect(B.cols,0,dft_B->cols-B.cols,B.rows)); cvZero(&submat_hdr);
    cvDFT( dft_B, dft_B, CV_DXT_FORWARD, B.rows );
    for ( int ni = 0; ni < nXplanes; ni++, xptr += Xsize ){
    CvMat A = cvMat(Xheight,Xwidth,CV_32F,xptr);
    CvMat C = cvMat(Yheight,Ywidth,CV_32F,yptr);
    cvGetSubRect( dft_A, &submat_hdr, cvRect(0,0,A.cols,A.rows)); cvCopy(&A,&submat_hdr);
    cvGetSubRect( dft_A, &submat_hdr, cvRect(A.cols,0,dft_A->cols-A.cols,A.rows)); cvZero(&submat_hdr);
    cvDFT( dft_A, dft_A, CV_DXT_FORWARD, A.rows );
    cvMulSpectrums( dft_A, dft_B, dft_A, 0);
    cvDFT( dft_A, dft_A, CV_DXT_INVERSE, C.rows+K-1 ); // calculate only the top part
    cvGetSubRect( dft_A, &submat_hdr, cvRect(K-1,K-1,C.cols,C.rows) );
    cvAddS(&submat_hdr, cvScalar(wptr[K*K]), &C); // bias
    } // ni
    } // no
    if (dft_A){cvReleaseMat(&dft_A);dft_A=0;}
    if (dft_B){cvReleaseMat(&dft_B);dft_B=0;}
  } // si

  cvScale(Y,Y,.5/float(K*K*K*K));
  // fprintf(stderr,"avg: %f, sdv: %f\n",cvAvg(Y).val[0],cvSdv(Y));
  if (!layer->WX){layer->WX=cvCloneMat(Y);}
  else if (layer->WX->rows==Y->rows){cvCopy(Y,layer->WX);}
  else{cvReleaseMat(&layer->WX);layer->WX=cvCloneMat(Y);}

  if (!strcmp(layer->activation,"none")){ // do nothing
  }else if (!strcmp(layer->activation,"tanh")){ CV_CALL(cvTanh( Y, Y ));
  }else if (!strcmp(layer->activation,"sigmoid")){ CV_CALL(cvSigmoid( Y, Y ));
  }else if (!strcmp(layer->activation,"relu")){ CV_CALL(cvReLU( Y, Y ));
  }else{CV_ERROR(CV_StsBadArg,"Unknown activation type");}

  CV_ASSERT(cvCountNAN(Y)<1);
  
  if (layer->Y){
    if (layer->Y->rows==Y->rows){cvCopy(Y,layer->Y);}else{cvReleaseMat(&layer->Y);layer->Y=cvCloneMat(Y);}
  }else{layer->Y=cvCloneMat(Y);}
  if (layer->visualize){icvVisualizeCNNLayer((CvDNNLayer*)layer,Y);}

  __END__;
}

/* <dE_dY>, <dE_dX> should be row-vectors.
   Function computes partial derivatives <dE_dX>
   of the loss function with respect to the planes components
   of the previous layer (X).
   It is a basic function for back propagation method.
   Input parameter <dE_dY> is the partial derivative of the
   loss function with respect to the planes components
   of the current layer. */
void icvCNNConvolutionBackward(
    CvDNNLayer * _layer, int t, const CvMat* X, const CvMat* _dE_dY, CvMat* dE_dX )
{
  CV_FUNCNAME("icvCNNConvolutionBackward");
  if ( !icvIsConvolutionLayer(_layer) ) { CV_ERROR( CV_StsBadArg, "Invalid layer" ); }

  __BEGIN__;

  CvDNNConvolutionLayer * layer = (CvDNNConvolutionLayer*) _layer;
  int n_output_layers = layer->output_layers.size();
  CvDNNLayer * ref_layer = layer->ref_layer;
  CvMat * weights = ref_layer?ref_layer->weights:layer->weights;
  
  const int K = layer->K;
  const int KK = K*K;

  const int n_X_planes     = layer->n_input_planes;
  const int Xheight = layer->input_height;
  const int Xwidth  = layer->input_width;
  const int X_plane_size   = Xheight*Xwidth;

  const int n_Y_planes     = layer->n_output_planes;
  const int Yheight = layer->output_height;
  const int Ywidth  = layer->output_width;
  const int Y_plane_size   = Yheight*Ywidth;

  const int batch_size = X->rows;
  CvMat * dE_dY = (CvMat*)_dE_dY;
  CvMat* dY_dX = 0;
  CvMat* dY_dW = 0;
  CvMat* dE_dW = 0;

  if (n_output_layers){
    dE_dY = cvCreateMat(batch_size,Y_plane_size*n_Y_planes,CV_32F); cvZero(dE_dY);
    for (int li=0;li<n_output_layers;li++){
      CvDNNLayer * output_layer = layer->output_layers[li];
      if (icvIsDenseLayer(output_layer)){
        cvAddWeighted(dE_dY,1.f,output_layer->dE_dX,1.f/float(n_output_layers),0.f,dE_dY);
      }
    } // average loss from all task
  }
  CvMat * dE_dY_afder = cvCreateMat(dE_dY->rows, dE_dY->cols, CV_32F); cvZero(dE_dY_afder);

  CV_ASSERT( t >= 1 );
  CV_ASSERT( n_Y_planes == weights->rows );

  if (layer->enable_cache){
    if (!layer->dY_dX){layer->dY_dX=cvCreateMat( n_Y_planes*Y_plane_size, X->cols, CV_32F );}
    dY_dX = layer->dY_dX;
  }else{
    dY_dX = cvCreateMat( n_Y_planes*Y_plane_size, X->cols, CV_32F );
  }
  dY_dW = cvCreateMat( dY_dX->rows, weights->cols*weights->rows, CV_32F );
  dE_dW = cvCreateMat( 1, dY_dW->cols, CV_32F );
  cvZero( dY_dX );
  cvZero( dY_dW );

  // compute gradient of the loss function with respect to X and W
#pragma omp parallel for
  for ( int si = 0; si < batch_size; si++ ){
    int yloc = 0;
    for ( int no = 0; no < n_Y_planes; no++, yloc += Y_plane_size ){
    int noKK = no*(KK+1);
    int xloc = 0;
    float * xptr = X->data.fl+X->cols*si;
    float * wptr = weights->data.fl + noKK;
    for ( int ni = 0; ni < n_X_planes; ni++, xptr += X_plane_size, xloc += X_plane_size ){
      for ( int yy = 0; yy < Xheight - K + 1; yy++ ){
      for ( int xx = 0; xx < Xwidth - K + 1; xx++ ){
#if 0
        for ( int ky = 0; ky < K; ky++ ){
        for ( int kx = 0; kx < K; kx++ ){
          int kidx = K*ky+kx; // weights
          int ridx = Ywidth*yy+xx;
          int cidx = Xwidth*(yy+ky)+(xx+kx);
          CV_MAT_ELEM(*dY_dX,float,yloc+ridx,xloc+cidx) = wptr[kidx];
          CV_MAT_ELEM(*dY_dW,float,yloc+ridx,noKK+kidx) += xptr[cidx];
        } // ky
        } // kx
        int ridx = Ywidth*yy+xx;
#else
        int ridx=Ywidth*yy+xx;
        int dydx_step=(yloc+ridx)*dY_dX->cols;
        int dydw_step=(yloc+ridx)*dY_dW->cols;
        for ( int ky = 0; ky < K; ky++ ){
        int kstep=K*ky; int xstep=Xwidth*(yy+ky);
        for ( int kx = 0; kx < K; kx++ ){
          int kidx=kstep+kx, cidx=xstep+(xx+kx);
          (dY_dX->data.fl+dydx_step)[xloc+cidx]=wptr[kidx];
          (dY_dW->data.fl+dydw_step)[noKK+kidx]+=xptr[cidx];
        } // ky
        } // kx
#endif
        CV_MAT_ELEM(*dY_dW, float, yloc+ridx, noKK+KK) += 1; // bias
      } // xx
      } // yy
    } // ni
    } // no
  } // si
  cvScale(dY_dW,dY_dW,1.f/float(batch_size));

  // dE_dY_afder = (tanh'(WX))*dE_dY
  if (!strcmp(layer->activation,"none")){
    cvCopy(dE_dY,dE_dY_afder);
  }else if (!strcmp(layer->activation,"tanh")){ 
    cvTanhDer(layer->WX,dE_dY_afder);
    cvMul(dE_dY_afder,dE_dY,dE_dY_afder);
  }else if (!strcmp(layer->activation,"sigmoid")){ 
    cvSigmoidDer(layer->WX,dE_dY_afder);
    cvMul(dE_dY_afder,dE_dY,dE_dY_afder);
  }else if (!strcmp(layer->activation,"relu")){ 
    cvReLUDer(layer->WX,dE_dY_afder);
    cvMul(dE_dY_afder,dE_dY,dE_dY_afder);
  }else{CV_ASSERT(false);}

  // dE_dW = sum( dE_dY * dY_dW )
  CvMat * dE_dW_ = cvCreateMat( batch_size, dY_dW->cols, CV_32FC1 );
  CV_CALL(cvGEMM( dE_dY_afder, dY_dW, 1.f,0,1.f,dE_dW_ )); 
  cvReduce(dE_dW_,dE_dW,-1,CV_REDUCE_SUM);
  cvReleaseMat(&dE_dW_);

  // dE_dX = dE_dY * dY_dX
  CV_CALL(cvGEMM( dE_dY_afder, dY_dX, 1.f,0,1.f,dE_dX ));

  // update weights
  {
    CvMat dE_dW_mat;
    float eta = -layer->init_learn_rate*cvInvSqrt((float)t);
    cvReshape( dE_dW, &dE_dW_mat, 0, weights->rows );
    if (!layer->dE_dW){
      ((CvDNNLayer*)layer)->dE_dW = cvCloneMat(&dE_dW_mat);
    }else{
      cvCopy(&dE_dW_mat,((CvDNNLayer*)layer)->dE_dW);
    }
    cvScaleAdd( &dE_dW_mat, cvRealScalar(eta), weights, weights );
  }

  if (n_output_layers){cvReleaseMat(&dE_dY);dE_dY=0;}
  if (!layer->enable_cache){ if (dY_dX){cvReleaseMat( &dY_dX );dY_dX=0;} }
  if (dE_dY_afder){cvReleaseMat( &dE_dY_afder );dE_dY_afder=0;}
  if (dY_dW){cvReleaseMat( &dY_dW );dY_dW=0;}
  if (dE_dW){cvReleaseMat( &dE_dW );dE_dW=0;}

  __END__;
}

void icvCNNConvolutionRelease( CvDNNLayer** p_layer )
{
  CV_FUNCNAME("icvCNNConvolutionRelease");
  __BEGIN__;

  CvDNNConvolutionLayer* layer = 0;

  if ( !p_layer )
      CV_ERROR( CV_StsNullPtr, "Null double pointer" );

  layer = *(CvDNNConvolutionLayer**)p_layer;

  if ( !layer )
      return;
  if ( !icvIsConvolutionLayer((CvDNNLayer*)layer) )
      CV_ERROR( CV_StsBadArg, "Invalid layer" );

  if (layer->weights){cvReleaseMat( &layer->weights );layer->weights=0;}
  cvReleaseMat( &layer->connect_mask );
  cvFree( p_layer );

  __END__;
}

