/*M////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to
//  this license. If you do not agree to this license, do not download,
//  install, copy or use the software.
//
//                        Intel License Agreement
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistribution's of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
//  * Redistribution's in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
//  * The name of Intel Corporation may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
//
// This software is provided by the copyright holders and contributors
// "as is" and any express or implied warranties, including, but not
// limited to, the implied warranties of merchantability and fitness for
// a particular purpose are disclaimed. In no event shall the
// Intel Corporation or contributors be liable for any direct, indirect,
// incidental, special, exemplary, or consequential damages (including,
// but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such
// damage.
//
//M*/

// #include "cvconfig.h"
// #include "opencv2/core/core_c.h"
// #include "opencv2/core/internal.hpp"
#include "_dnn.h"

// #include "precomp.hpp"

#include "cnn.h"
// #include "cvext.h"
// #include "_dnn.h"

/*************************************************************************\
 *               Auxilary functions declarations                         *
\*************************************************************************/

/*------------ functions for the CNN classifier --------------------*/
static float icvCNNModelPredict(
        const CvCNNStatModel* cnn_model,
        const CvMat* image,
        CvMat* probs CV_DEFAULT(0) );
static void icvCNNModelUpdate(
        CvCNNStatModel* cnn_model, const CvMat* images, int tflag,
        const CvMat* responses, const CvStatModelParams* params,
        const CvMat* CV_DEFAULT(0), const CvMat* sample_idx CV_DEFAULT(0),
        const CvMat* CV_DEFAULT(0), const CvMat* CV_DEFAULT(0));
static void icvCNNModelRelease( CvCNNStatModel** cnn_model );

static void icvTrainCNNetwork( CvCNNetwork* network,
        const CvMat* images, const CvMat* responses, const CvMat* etalons,
        int grad_estim_type, int max_iter, int start_iter, int batch_size);

/*-------------- functions for the CNN network -------------------------*/
static void icvCNNetworkAddLayer( CvCNNetwork* network, CvCNNLayer* layer );
static CvCNNLayer* icvCNNetworkGetLayer( CvCNNetwork* network, const char * name );
static void icvCNNetworkRelease( CvCNNetwork** network );
static CvCNNetwork * icvCNNetworkRead( CvFileStorage * fs );
static void icvCNNetworkWrite( CvCNNetwork * struct_ptr, CvFileStorage * fs );
/* In all layer functions we denote input by X and output by Y, where
   X and Y are column-vectors, so that
   length(X)==<n_input_planes>*<input_height>*<input_width>,
   length(Y)==<n_output_planes>*<output_height>*<output_width>.
*/

/*--------------------------- utility functions -----------------------*/
static float icvEvalAccuracy(CvCNNLayer * last_layer, CvMat * result, CvMat * expected);

/**************************************************************************\
 *                 Functions implementations                              *
\**************************************************************************/

static void icvCheckCNNetwork(CvCNNetwork * network,
                              CvCNNStatModelParams * params,
                              int img_size,
                              char * cvFuncName)  
{                                              
  CvCNNLayer* first_layer, *layer, *last_layer;
  int n_layers, i;                             
  if ( !network ) {
    CV_ERROR( CV_StsNullPtr, "Null <network> pointer. Network must be created by user." ); 
  }
  n_layers = network->n_layers;                                    
  first_layer = last_layer = network->first_layer;                      
  for ( i = 0, layer = first_layer; i < n_layers && layer; i++ ) {
    if ( !icvIsCNNLayer(layer) ) {
      CV_ERROR( CV_StsNullPtr, "Invalid network" );
    }
    last_layer = layer;                                            
    layer = layer->next_layer;                                     
  }                                                                
  if ( i == 0 || i != n_layers || first_layer->prev_layer || layer ){
    CV_ERROR( CV_StsNullPtr, "Invalid network" );
  }
  if (icvIsCNNConvolutionLayer(first_layer)){
    if ( first_layer->n_input_planes != 1 ) {                  
      CV_ERROR( CV_StsBadArg, "First layer must contain only one input plane" );
    }
    if ( img_size != first_layer->input_height*first_layer->input_width ){
      CV_ERROR( CV_StsBadArg, "Invalid input sizes of the first layer" );
    }
  }
  if (icvIsCNNRecurrentNNLayer(last_layer)){
    if ( params->etalons->cols != last_layer->n_output_planes*
         ((CvCNNRecurrentLayer*)last_layer)->seq_length*
         last_layer->output_height*last_layer->output_width ) {
      CV_ERROR( CV_StsBadArg, "Invalid output sizes of the last layer" );
    }
  }else{
    if ( params->etalons->cols != last_layer->n_output_planes* 
         last_layer->output_height*last_layer->output_width ) {
      CV_ERROR( CV_StsBadArg, "Invalid output sizes of the last layer" );
    }
  }
}

static void icvCheckCNNModelParams(CvCNNStatModelParams * params,
                                   CvCNNStatModel * cnn_model,
                                   char * cvFuncName)
{                                                                 
  if ( !params ) {
    CV_ERROR( CV_StsNullPtr, "Null <params> pointer" );           
  }
  if ( !ICV_IS_MAT_OF_TYPE(params->etalons, CV_32FC1) ) {
    CV_ERROR( CV_StsBadArg, "<etalons> must be CV_32FC1 type" );  
  }
  if ( params->etalons->rows != cnn_model->cls_labels->cols ) {
    CV_ERROR( CV_StsBadArg, "Invalid <etalons> size" );
  }
  if ( params->grad_estim_type != CV_CNN_GRAD_ESTIM_RANDOM &&      
      params->grad_estim_type != CV_CNN_GRAD_ESTIM_BY_WORST_IMG ) {
    CV_ERROR( CV_StsBadArg, "Invalid <grad_estim_type>" );        
  }
  if ( params->start_iter < 0 ) {
    CV_ERROR( CV_StsBadArg, "Parameter <start_iter> must be positive or zero" ); 
  }
  if ( params->max_iter < 1 ) {
    params->max_iter = 1;
  }
}

/********************************************************************\
 *                    Classifier functions                          *
\********************************************************************/

ML_IMPL CvCNNStatModel*	cvCreateCNNStatModel(int flag, int size)// ,
    // CvCNNStatModelRelease release,
		// CvCNNStatModelPredict predict,
		// CvCNNStatModelUpdate update)
{
  CvCNNStatModel *p_model;
  CV_FUNCNAME("cvCreateStatModel");
  __CV_BEGIN__;
  //add the implementation here
  CV_CALL(p_model = (CvCNNStatModel*) cvAlloc(sizeof(*p_model)));
  memset(p_model, 0, sizeof(*p_model));
  p_model->release = icvCNNModelRelease; //release;
  p_model->update = icvCNNModelUpdate;// NULL;
  p_model->predict = icvCNNModelPredict;// NULL;
  __CV_END__;
  if (cvGetErrStatus() < 0) {
    CvCNNStatModel* base_ptr = (CvCNNStatModel*) p_model;
    if (p_model && p_model->release) {
      p_model->release(&base_ptr);
    }else{
      cvFree(&p_model);
    }
    p_model = 0;
  }
  return (CvCNNStatModel*) p_model;
  return NULL;
}

ML_IMPL CvCNNStatModel*
cvTrainCNNClassifier( const CvMat* _train_data, int tflag,
            const CvMat* _responses,
            const CvCNNStatModelParams* _params, 
            const CvMat*, const CvMat* _sample_idx, const CvMat*, const CvMat* )
{
  CvCNNStatModel* cnn_model    = 0;
  CvMat * train_data = cvCloneMat(_train_data);
  CvMat* responses             = 0;

  CV_FUNCNAME("cvTrainCNNClassifier");
  __BEGIN__;

  int n_images;
  int img_size;
  CvCNNStatModelParams* params = (CvCNNStatModelParams*)_params;

  CV_CALL(cnn_model = (CvCNNStatModel*)
    cvCreateCNNStatModel(CV_STAT_MODEL_MAGIC_VAL|CV_CNN_MAGIC_VAL, sizeof(CvCNNStatModel)));
  
  img_size = _train_data->cols;
  n_images = _train_data->rows;
  cnn_model->cls_labels = params->cls_labels;
  // responses = cvCreateMat(_responses->cols,n_images,CV_32S);
  // cvTranspose(_responses,responses);
  responses = cvCreateMat(n_images,_responses->cols,CV_32F);
  cvConvert(_responses,responses);
  CV_ASSERT(CV_MAT_TYPE(train_data->type)==CV_32F);

  // normalize image value range
  if (icvIsCNNConvolutionLayer(params->network->first_layer->next_layer)){
    double minval, maxval;
    cvMinMaxLoc(train_data,&minval,&maxval,0,0);
    cvSubS(train_data,cvScalar(minval),train_data);
    cvScale(train_data,train_data,10./((maxval-minval)*.5f));
    cvAddS(train_data,cvScalar(-1.f),train_data);
  }

  icvCheckCNNModelParams(params,cnn_model,cvFuncName);
  icvCheckCNNetwork(params->network,params,img_size,cvFuncName);

  cnn_model->network = params->network;
  CV_CALL(cnn_model->etalons = cvCloneMat( params->etalons ));

  CV_CALL( icvTrainCNNetwork( cnn_model->network, train_data, responses,
                              cnn_model->etalons, params->grad_estim_type, 
                              params->max_iter, params->start_iter, params->batch_size ));
  __END__;

  if ( cvGetErrStatus() < 0 && cnn_model ){
    cnn_model->release( (CvCNNStatModel**)&cnn_model );
  }
  cvReleaseMat( &train_data );
  cvReleaseMat( &responses );

  return (CvCNNStatModel*)cnn_model;
}

/*************************************************************************/
static void icvTrainCNNetwork( CvCNNetwork* network,const CvMat* images, const CvMat* responses,
                               const CvMat* , //etalons,
                               int grad_estim_type, int max_iter, int start_iter, int batch_size )
{
  CvMat** X     = 0;
  CvMat** dE_dX = 0;
  const int n_layers = network->n_layers;
  int k;

  CV_FUNCNAME("icvTrainCNNetwork");
  __BEGIN__;

  CvCNNLayer * first_layer = network->first_layer;
  CvCNNLayer * last_layer = cvGetCNNLastLayer(network);
  // const int seq_length = first_layer->seq_length;
  const int img_size   =
    first_layer->n_input_planes*first_layer->input_width*first_layer->input_height;
  const int n_images   = responses->rows; CV_ASSERT(n_images==images->rows);
  CvMat * X0_transpose = cvCreateMat( batch_size*first_layer->seq_length, img_size, CV_32FC1 );
  CvCNNLayer* layer;
  int n;
  CvRNG rng = cvRNG(-1);

  CV_CALL(X = (CvMat**)cvAlloc( (n_layers+1)*sizeof(CvMat*) ));
  CV_CALL(dE_dX = (CvMat**)cvAlloc( (n_layers+1)*sizeof(CvMat*) ));
  memset( X, 0, (n_layers+1)*sizeof(CvMat*) );
  memset( dE_dX, 0, (n_layers+1)*sizeof(CvMat*) );

  // initialize input data
  CV_CALL(X[0] = cvCreateMat( img_size, batch_size*first_layer->seq_length, CV_32F ));
  CV_CALL(dE_dX[0] = cvCreateMat( batch_size*first_layer->seq_length, X[0]->rows, CV_32F ));
  cvZero(X[0]); cvZero(dE_dX[0]); cvZero(X0_transpose);
  for ( k = 0, layer = first_layer; k < n_layers; k++, layer = layer->next_layer ){
    int n_outputs = layer->n_output_planes*layer->output_height*layer->output_width;
    int seq_length = layer->seq_length;
    CV_CALL(X[k+1] = cvCreateMat( n_outputs, batch_size*seq_length, CV_32F )); 
    CV_CALL(dE_dX[k+1] = cvCreateMat( batch_size*seq_length, X[k+1]->rows, CV_32F )); 
    cvZero(X[k+1]); cvZero(dE_dX[k+1]);
  }

  for ( n = 1; n <= max_iter; n++ )
  {
    float loss, max_loss = 0;
    int i;
    int nclasses = X[n_layers]->rows;
    CvMat * worst_img_idx = cvCreateMat(batch_size,1,CV_32S);
    int * right_etal_idx = responses->data.i;
    CvMat * etalon = cvCreateMat(batch_size*last_layer->seq_length,nclasses,CV_32F);

    // Use the random image
    if (first_layer->seq_length>1 && img_size!=images->cols){
      cvRandArr(&rng,worst_img_idx,CV_RAND_UNI,cvScalar(0),cvScalar(n_images-first_layer->seq_length));
    }else{
      cvRandArr(&rng,worst_img_idx,CV_RAND_UNI,cvScalar(0),cvScalar(n_images-1));
    }
    // cvPrintf(stderr,"%d, ",worst_img_idx);

    // 1) Compute the network output on the <X0_transpose>
    CV_ASSERT(CV_MAT_TYPE(X0_transpose->type)==CV_32F && CV_MAT_TYPE(images->type)==CV_32F);
    CV_ASSERT(img_size==X0_transpose->cols && img_size*first_layer->seq_length==images->cols);
    for ( k = 0; k < batch_size; k++ ){
      memcpy(X0_transpose->data.fl+images->cols*k,
             images->data.fl+images->cols*worst_img_idx->data.i[k],
             sizeof(float)*images->cols);
    }
    CV_CALL(cvTranspose( X0_transpose, X[0] ));

    // Perform prediction with current weight parameters
    for ( k = 0, layer = first_layer; k < n_layers - 1; k++, layer = layer->next_layer ){
      CV_CALL(layer->forward( layer, X[k], X[k+1] )); 
    }
    CV_CALL(layer->forward( layer, X[k], X[k+1] ));

    // 2) Compute the gradient
    CvMat etalon_src, etalon_dst;
    cvTranspose( X[n_layers], dE_dX[n_layers] );
    for ( k = 0; k < batch_size; k++ ){
      cvGetRow(responses,&etalon_src,worst_img_idx->data.i[k]);
      cvReshape(etalon,&etalon_dst,0,batch_size);
      cvCopy(&etalon_src, &etalon_dst);
    }
    cvSub( dE_dX[n_layers], etalon, dE_dX[n_layers] );

    // 3) Update weights by the gradient descent
    for ( k = n_layers; k > 0; k--, layer = layer->prev_layer ){
      CV_CALL(layer->backward( layer, n + start_iter, X[k-1], dE_dX[k], dE_dX[k-1] ));
    }

#if 1        
    // print progress
    CvMat * etalon_transpose = cvCreateMat(etalon->cols,etalon->rows,CV_32F);
    cvTranspose(etalon,etalon_transpose);
    float trloss = cvNorm(X[n_layers], etalon_transpose)/float(batch_size);
    float top1 = icvEvalAccuracy(last_layer, X[n_layers], etalon_transpose);
    static double sumloss = 0; sumloss += trloss;
    static double sumacc  = 0; sumacc  += top1;
    if (int(float(n*100)/float(max_iter))<int(float((n+1)*100)/float(max_iter))){
      fprintf(stderr, "%d/%d = %.0f%%,",n+1,max_iter,float(n*100.f)/float(max_iter));
      fprintf(stderr, "sumacc: %.1f%%[%.1f%%], sumloss: %f\n", sumacc/float(n),top1,sumloss/float(n));
      CvMat * Xn_transpose = cvCreateMat(batch_size*last_layer->seq_length,last_layer->n_output_planes,CV_32F);
      cvTranspose(X[n_layers],Xn_transpose);
      {fprintf(stderr,"input:\n");cvPrintf(stderr,"%.0f ", X0_transpose);}
      {fprintf(stderr,"output:\n");cvPrintf(stderr,"%.1f ", Xn_transpose);}
      {fprintf(stderr,"expect:\n");cvPrintf(stderr,"%.1f ", etalon);}
      cvReleaseMat(&Xn_transpose);
    }
    cvReleaseMat(&etalon_transpose);
#endif
    if (etalon){cvReleaseMat(&etalon);etalon=0;}
  }
  if (X0_transpose){cvReleaseMat(&X0_transpose);X0_transpose=0;}
  __END__;

  for ( k = 0; k <= n_layers; k++ ){
    cvReleaseMat( &X[k] );
    cvReleaseMat( &dE_dX[k] );
  }
  cvFree( &X );
  cvFree( &dE_dX );
}

static float icvEvalAccuracy(CvCNNLayer * last_layer, CvMat * result, CvMat * expected)
{
  CV_FUNCNAME("icvEvalAccuracy");
  float top1 = 0;
  int n_outputs = last_layer->n_output_planes;
  int seq_length = icvIsCNNRecurrentNNLayer(last_layer)?
    ((CvCNNRecurrentLayer*)last_layer)->seq_length:1;
  int batch_size = result->cols;
  CvMat * sorted = cvCreateMat(result->rows,result->cols,CV_32F);
  CvMat * indices = cvCreateMat(result->rows,result->cols,CV_32S);
  CvMat * indtop1 = cvCreateMat(1,result->cols,CV_32S);
  CvMat * expectedmat = cvCreateMat(1,result->cols,CV_32S);
  CvMat * indtop1true = cvCreateMat(result->rows,result->cols,CV_32S);
  CvMat * indtop1res = cvCreateMat(1,result->cols,CV_8U);
  __BEGIN__;
  cvSort(result,sorted,indices,CV_SORT_DESCENDING|CV_SORT_EVERY_COLUMN);
  cvGetRow(indices,indtop1,0);
  cvSort(expected,0,indtop1true,CV_SORT_DESCENDING|CV_SORT_EVERY_COLUMN);
  assert( CV_MAT_TYPE(indtop1true->type) == CV_32S && CV_MAT_TYPE(expectedmat->type) == CV_32S );
  for (int ii=0;ii<indtop1true->cols;ii++){
    CV_MAT_ELEM(*expectedmat,int,0,ii)=CV_MAT_ELEM(*indtop1true,int,0,ii); // transpose and convert
  }
  cvCmp(indtop1,expectedmat,indtop1res,CV_CMP_EQ);
  top1=cvSum(indtop1res).val[0]*100.f/float(batch_size)/255.f;
  __END__;
  cvReleaseMat(&sorted);
  cvReleaseMat(&indices);
  cvReleaseMat(&indtop1);
  cvReleaseMat(&expectedmat);
  cvReleaseMat(&indtop1true);
  cvReleaseMat(&indtop1res);
  return top1;
}

/*************************************************************************/
static float icvCNNModelPredict( const CvCNNStatModel* model, const CvMat* _image, CvMat* probs )
{
  CvMat** X       = 0;
  float* img_data = 0;
  int n_layers = 0;
  int best_etal_idx = -1;
  int k;

  CV_FUNCNAME("icvCNNModelPredict");
  __BEGIN__;

  CvCNNStatModel* cnn_model = (CvCNNStatModel*)model;
  CvCNNLayer* first_layer=0, *layer = 0;
  int img_height, img_width, img_size;
  int nclasses, i;
  float loss, min_loss = FLT_MAX;
  float* probs_data;
  CvMat etalon, X0_transpose;
  int nsamples;
  int n_inputs, seq_length;

  if ( model==0 ) { CV_ERROR( CV_StsBadArg, "Invalid model" ); }

  nclasses = cnn_model->cls_labels->cols;
  n_layers = cnn_model->network->n_layers;
  first_layer = cnn_model->network->first_layer;
  n_inputs = first_layer->n_input_planes;
  seq_length = icvIsCNNInputDataLayer(first_layer)?
    ((CvCNNInputDataLayer*)first_layer)->seq_length:1;
  img_height = first_layer->input_height;
  img_width = first_layer->input_width;
  img_size = img_height*img_width*n_inputs*seq_length;
  nsamples = _image->rows;
  
#if 0
  cvPreparePredictData( _image, img_size, 0, nclasses, probs, &img_data );
#else
  CV_ASSERT(nsamples==probs->cols);
  CV_ASSERT(_image->cols==img_size && nsamples==_image->rows);
  if (!img_data){img_data = (float*)cvAlloc(img_size*nsamples*sizeof(float));}
  CvMat imghdr = cvMat(_image->rows,_image->cols,CV_32F,img_data);
  cvCopy(_image,&imghdr);
#endif

  // normalize image value range
  if (icvIsCNNConvolutionLayer(cnn_model->network->first_layer->next_layer)){
    double minval, maxval;
    cvMinMaxLoc(&imghdr,&minval,&maxval,0,0);
    cvAddS(&imghdr,cvScalar(-minval),&imghdr);
    cvScale(&imghdr,&imghdr,10./((maxval-minval)*.5f));
    cvAddS(&imghdr,cvScalar(-1.f),&imghdr);
  }

  CV_CALL(X = (CvMat**)cvAlloc( (n_layers+1)*sizeof(CvMat*) ));
  memset( X, 0, (n_layers+1)*sizeof(CvMat*) );

  CV_CALL(X[0] = cvCreateMat( img_size,nsamples,CV_32FC1 ));
  for ( k = 0, layer = first_layer; k < n_layers; k++, layer = layer->next_layer ){
    CV_CALL(X[k+1] = cvCreateMat( layer->n_output_planes*layer->output_height*
                                  layer->output_width, nsamples, CV_32FC1 ));
  }

  X0_transpose = cvMat( nsamples, img_size, CV_32FC1, img_data );
  cvTranspose( &X0_transpose, X[0] );
  for ( k = 0, layer = first_layer; k < n_layers; k++, layer = layer->next_layer ) 
#if 1
  {CV_CALL(layer->forward( layer, X[k], X[k+1] ));}
#else
  { // if (k==4){cvScale(X[k],X[k],1./255.);}
    CV_CALL(layer->forward( layer, X[k], X[k+1] ));
    icvVisualizeCNNLayer(layer, X[k+1]);
  }fprintf(stderr,"\n");
#endif

  cvCopy(X[n_layers],probs);

  __END__;

  for ( k = 0; k <= n_layers; k++ )
    cvReleaseMat( &X[k] );
  cvFree( &X );
  if ( img_data != _image->data.fl )
    cvFree( &img_data );

  return ((float) ((CvCNNStatModel*)model)->cls_labels->data.i[best_etal_idx]);
}

/****************************************************************************************/
static void icvCNNModelUpdate(
        CvCNNStatModel* _cnn_model, const CvMat* _train_data, int tflag,
        const CvMat* _responses, const CvStatModelParams* _params,
        const CvMat*, const CvMat* _sample_idx,
        const CvMat*, const CvMat* )
{
    const float** out_train_data = 0;
    CvMat* responses             = 0;
    CvMat* cls_labels            = 0;

    CV_FUNCNAME("icvCNNModelUpdate");
    __BEGIN__;

    int n_images, img_size, i;
    CvCNNStatModelParams* params = (CvCNNStatModelParams*)_params;
    CvCNNStatModel* cnn_model = (CvCNNStatModel*)_cnn_model;

    if ( cnn_model==0 ) {
        CV_ERROR( CV_StsBadArg, "Invalid model" );
    }

    // CV_CALL(cvPrepareTrainData( "cvTrainCNNClassifier",
    //     _train_data, tflag, _responses, CV_VAR_CATEGORICAL,
    //     0, _sample_idx, false, &out_train_data,
    //     &n_images, &img_size, &img_size, &responses,
    //     &cls_labels, 0, 0 ));

    // ICV_CHECK_CNN_MODEL_PARAMS(params);
    icvCheckCNNModelParams(params,cnn_model,cvFuncName);

    // Number of classes must be the same as when classifiers was created
    if ( !CV_ARE_SIZES_EQ(cls_labels, cnn_model->cls_labels) ) {
        CV_ERROR( CV_StsBadArg, "Number of classes must be left unchanged" );
    }
    for ( i = 0; i < cls_labels->cols; i++ ) {
      if ( cls_labels->data.i[i] != cnn_model->cls_labels->data.i[i] ) {
            CV_ERROR( CV_StsBadArg, "Number of classes must be left unchanged" );
      }
    }

    CV_CALL( icvTrainCNNetwork( cnn_model->network, _train_data, responses,
        cnn_model->etalons, params->grad_estim_type, params->max_iter,
        params->start_iter, params->batch_size ));

    __END__;

    cvFree( &out_train_data );
    cvReleaseMat( &responses );
}

/*************************************************************************/
static void icvCNNModelRelease( CvCNNStatModel** cnn_model )
{
    CV_FUNCNAME("icvCNNModelRelease");
    __BEGIN__;

    CvCNNStatModel* cnn;
    if ( !cnn_model )
        CV_ERROR( CV_StsNullPtr, "Null double pointer" );

    cnn = *(CvCNNStatModel**)cnn_model;

    cvReleaseMat( &cnn->cls_labels );
    cvReleaseMat( &cnn->etalons );
    cnn->network->release( &cnn->network );

    cvFree( &cnn );

    __END__;

}

/************************************************************************ \
 *                       Network functions                              *
\************************************************************************/
ML_IMPL CvCNNetwork* cvCreateCNNetwork( CvCNNLayer* first_layer )
{
    CvCNNetwork* network = 0;

    CV_FUNCNAME( "cvCreateCNNetwork" );
    __BEGIN__;

    if ( !icvIsCNNLayer(first_layer) )
        CV_ERROR( CV_StsBadArg, "Invalid layer" );

    CV_CALL(network = (CvCNNetwork*)cvAlloc( sizeof(CvCNNetwork) ));
    memset( network, 0, sizeof(CvCNNetwork) );

    network->first_layer    = first_layer;
    network->n_layers  = 1;
    network->release   = icvCNNetworkRelease;
    network->add_layer = icvCNNetworkAddLayer;
    network->get_layer = icvCNNetworkGetLayer;
    network->read      = icvCNNetworkRead;
    network->write     = icvCNNetworkWrite;

    __END__;

    if ( cvGetErrStatus() < 0 && network )
        cvFree( &network );

    return network;

}

/***********************************************************************/
static void icvCNNetworkAddLayer( CvCNNetwork* network, CvCNNLayer* layer )
{
  CV_FUNCNAME( "icvCNNetworkAddLayer" );
  __BEGIN__;

  CvCNNLayer* prev_layer;

  if ( network == NULL ) {
    CV_ERROR( CV_StsNullPtr, "Null <network> pointer" );
  }

  // prev_layer = network->first_layer;
  // while ( prev_layer->next_layer ) { prev_layer = prev_layer->next_layer; }
  prev_layer = cvGetCNNLastLayer(network);

  if ( icvIsCNNFullConnectLayer(layer) ){
    if ( ((CvCNNFullConnectLayer*)layer)->input_layers.size()==0 && 
         layer->n_input_planes != prev_layer->output_width*prev_layer->output_height*
         prev_layer->n_output_planes ) {
      CV_ERROR( CV_StsBadArg, "Unmatched size of the new layer" );
    }
    if ( layer->input_height != 1 || layer->output_height != 1 ||
         layer->input_width != 1  || layer->output_width != 1 ) {
      CV_ERROR( CV_StsBadArg, "Invalid size of the new layer" );
    }
  }else if ( icvIsCNNConvolutionLayer(layer) || icvIsCNNSubSamplingLayer(layer) ){
    if ( prev_layer->n_output_planes != layer->n_input_planes ||
         prev_layer->output_height   != layer->input_height ||
         prev_layer->output_width    != layer->input_width ) {
      CV_ERROR( CV_StsBadArg, "Unmatched size of the new layer" );
    }
  }else if ( icvIsCNNRecurrentNNLayer(layer) ) {
    if ( layer->input_height != 1 || layer->output_height != 1 ||
         layer->input_width != 1  || layer->output_width != 1 ) {
      CV_ERROR( CV_StsBadArg, "Invalid size of the new layer" );
    }
  }else if ( icvIsCNNImgCroppingLayer(layer) ) {
  }else if ( icvIsCNNMultiTargetLayer(layer) ) {
    CV_ASSERT(((CvCNNMultiTargetLayer*)layer)->input_layers.size()>=1);
    CV_ASSERT(((CvCNNMultiTargetLayer*)layer)->input_layers.size()<=100);
  }else{
    CV_ERROR( CV_StsBadArg, "Invalid layer" );
  }

  layer->prev_layer = prev_layer;
  prev_layer->next_layer = layer;
  network->n_layers++;

  __END__;
}

static CvCNNLayer* icvCNNetworkGetLayer( CvCNNetwork* network, const char * name )
{
  CV_FUNCNAME("icvGetCNNGetLayer");
  CvCNNLayer* first_layer, *layer, *last_layer, *target_layer=0;
  int n_layers, i;
  __BEGIN__;
  if ( !network ) {
    CV_ERROR( CV_StsNullPtr, "Null <network> pointer. Network must be created by user." ); 
  }
  n_layers = network->n_layers;
  first_layer = last_layer = network->first_layer;
  for ( i = 0, layer = first_layer; i < n_layers && layer; i++ ) {
    if ( !icvIsCNNLayer(layer) ) {
      CV_ERROR( CV_StsNullPtr, "Invalid network" );
    }
    if (!strcmp(layer->name,name)){target_layer=layer;break;}
    last_layer = layer;
    layer = layer->next_layer;
  }
  __END__;
  return target_layer;
}

CvCNNLayer * cvGetCNNLastLayer(CvCNNetwork * network)
{
  CV_FUNCNAME("icvGetCNNLastLayer");
  CvCNNLayer* first_layer, *layer, *last_layer;
  int n_layers, i;
  __BEGIN__;
  if ( !network ) {
    CV_ERROR( CV_StsNullPtr, "Null <network> pointer. Network must be created by user." ); 
  }
  n_layers = network->n_layers;
  first_layer = last_layer = network->first_layer;
  for ( i = 0, layer = first_layer; i < n_layers && layer; i++ ) {
    if ( !icvIsCNNLayer(layer) ) {
      CV_ERROR( CV_StsNullPtr, "Invalid network" );
    }
    last_layer = layer;
    layer = layer->next_layer;
  }
  __END__;
  return last_layer;
}

/*************************************************************************/
static void icvCNNetworkRelease( CvCNNetwork** network_pptr )
{
    CV_FUNCNAME( "icvReleaseCNNetwork" );
    __BEGIN__;

    CvCNNetwork* network = 0;
    CvCNNLayer* layer = 0, *next_layer = 0;
    int k;

    if ( network_pptr == NULL )
        CV_ERROR( CV_StsBadArg, "Null double pointer" );
    if ( *network_pptr == NULL )
        return;

    network = *network_pptr;
    layer = network->first_layer;
    if ( layer == NULL )
        CV_ERROR( CV_StsBadArg, "CNN is empty (does not contain any layer)" );

    // k is the number of the layer to be deleted
    for ( k = 0; k < network->n_layers && layer; k++ )
    {
        next_layer = layer->next_layer;
        layer->release( &layer );
        layer = next_layer;
    }

    if ( k != network->n_layers || layer)
        CV_ERROR( CV_StsBadArg, "Invalid network" );

    cvFree( &network );

    __END__;
}

/*************************************************************************\
 *                        Layer functions                                *
\*************************************************************************/
CvCNNLayer* icvCreateCNNLayer( int layer_type, 
    const int dtype, const char * name, int header_size,
    int n_input_planes, int input_height, int input_width,
    int n_output_planes, int output_height, int output_width,
    float init_learn_rate, int learn_rate_decrease_type,
    CvCNNLayerRelease release, CvCNNLayerForward forward, CvCNNLayerBackward backward )
{
  CvCNNLayer* layer = 0;

  CV_FUNCNAME("icvCreateCNNLayer");
  __BEGIN__;

  CV_ASSERT( release && forward && backward )
  CV_ASSERT( header_size >= sizeof(CvCNNLayer) )

  if ( n_input_planes < 1 || n_output_planes < 1 ||
       input_height   < 1 || input_width < 1 ||
       output_height  < 1 || output_width < 1 ||
       input_height < output_height || input_width  < output_width ) 
  {
    CV_ERROR( CV_StsBadArg, "Incorrect input or output parameters" );
  }
  if ( init_learn_rate < FLT_EPSILON ) {
    CV_ERROR( CV_StsBadArg, "Initial learning rate must be positive" );
  }
  if ( learn_rate_decrease_type != CV_CNN_LEARN_RATE_DECREASE_HYPERBOLICALLY &&
       learn_rate_decrease_type != CV_CNN_LEARN_RATE_DECREASE_SQRT_INV &&
       learn_rate_decrease_type != CV_CNN_LEARN_RATE_DECREASE_LOG_INV ) 
  {
    CV_ERROR( CV_StsBadArg, "Invalid type of learning rate dynamics" );
  }

  CV_CALL(layer = (CvCNNLayer*)cvAlloc( header_size ));
  memset( layer, 0, header_size );

  layer->flags = ICV_CNN_LAYER|layer_type;
  CV_ASSERT( icvIsCNNLayer(layer) );

  layer->dtype = dtype;
  strcpy(layer->name,name);

  layer->n_input_planes = n_input_planes;
  layer->input_height   = input_height;
  layer->input_width    = input_width;

  layer->n_output_planes = n_output_planes;
  layer->output_height   = output_height;
  layer->output_width    = output_width;

  layer->init_learn_rate = init_learn_rate;
  layer->decay_type = learn_rate_decrease_type;

  layer->release  = release;
  layer->forward  = forward;
  layer->backward = backward;

  __END__;

  if ( cvGetErrStatus() < 0 && layer) { cvFree( &layer ); }

  return layer;
}

void icvVisualizeCNNLayer(CvCNNLayer * layer, const CvMat * Y)
{
  CV_FUNCNAME("icvVisualizeCNNLayer");
  int hh = layer->output_height;
  int ww = layer->output_width;
  int nplanes = layer->n_output_planes;
  int nsamples = Y->cols;
  CvMat * imgY = cvCreateMat(hh*nsamples,ww*nplanes,CV_32F);
  float * imgYptr = imgY->data.fl;
  float * Yptr = Y->data.fl;
  int imgYcols = imgY->cols;
  __BEGIN__;
  CV_ASSERT(imgY->cols*imgY->rows == Y->cols*Y->rows);
  for (int si=0;si<nsamples;si++){
  for (int pi=0;pi<nplanes;pi++){
    for (int yy=0;yy<hh;yy++){
    for (int xx=0;xx<ww;xx++){
      CV_MAT_ELEM(*imgY,float,hh*si+yy,ww*pi+xx)=CV_MAT_ELEM(*Y,float,hh*ww*pi+ww*yy+xx,si);
    }
    }
  }
  }
  fprintf(stderr,"%dx%d,",imgY->cols,imgY->rows);
  CV_SHOW(imgY);
  __END__;
  cvReleaseMat(&imgY);
}


/*************************************************************************\
 *                           Utility functions                           *
\*************************************************************************/
void cvTanh(CvMat * src, CvMat * dst)
{
  CV_FUNCNAME("cvTanh");
  int ii,elemsize=src->rows*src->cols;
  __CV_BEGIN__
  {
  CV_ASSERT(src->rows==dst->rows && src->cols==dst->cols);
  CV_ASSERT(CV_MAT_TYPE(src->type)==CV_MAT_TYPE(dst->type));
  if (CV_MAT_TYPE(src->type)==CV_32F){
    float * srcptr = src->data.fl;
    float * dstptr = dst->data.fl;
    for (ii=0;ii<elemsize;ii++){
      dstptr[ii] = tanh(srcptr[ii]);
    }
  }else if (CV_MAT_TYPE(src->type)==CV_64F){
    double * srcptr = src->data.db;
    double * dstptr = dst->data.db;
    for (ii=0;ii<elemsize;ii++){
      dstptr[ii] = tanh(srcptr[ii]);
    }
  }else{
    CV_ERROR(CV_StsBadArg,"Unsupported data type");
  }
  }
  __CV_END__
}

void cvTanhDer(CvMat * src, CvMat * dst) {
  CV_FUNCNAME("cvTanhDer");
  int ii,elemsize=src->rows*src->cols;
  __CV_BEGIN__
  {
  CV_ASSERT(src->rows==dst->rows && src->cols==dst->cols);
  CV_ASSERT(CV_MAT_TYPE(src->type)==CV_MAT_TYPE(dst->type));
  if (CV_MAT_TYPE(src->type)==CV_32F){
    float * srcptr = src->data.fl;
    float * dstptr = dst->data.fl;
    for (ii=0;ii<elemsize;ii++){
      dstptr[ii] = 1.f-pow(tanh(srcptr[ii]),2);
    }
  }else if (CV_MAT_TYPE(src->type)==CV_64F){
    double * srcptr = src->data.db;
    double * dstptr = dst->data.db;
    for (ii=0;ii<elemsize;ii++){
      dstptr[ii] = 1.f-pow(tanh(srcptr[ii]),2);
    }
  }else{
    CV_ERROR(CV_StsBadArg,"Unsupported data type");
  }
  }
  __CV_END__
}
  
/****************************************************************************************\
*                              Read/Write CNN classifier                                *
\****************************************************************************************/
static int icvIsCNNModel( const void* ptr )
{
  return (ptr!=0);
}

/****************************************************************************************/
static void icvReleaseCNNModel( void** ptr )
{
  CV_FUNCNAME("icvReleaseCNNModel");
  __BEGIN__;

  if ( !ptr ) { CV_ERROR( CV_StsNullPtr, "NULL double pointer" ); }
  CV_ASSERT((*ptr)!=0);

  icvCNNModelRelease( (CvCNNStatModel**)ptr );

  __END__;
}

/****************************************************************************************/
static CvCNNLayer* icvReadCNNLayer( CvFileStorage* fs, CvFileNode* node )
{
  CvCNNLayer* layer = 0;
  CvMat* weights    = 0;
  CvMat* connect_mask = 0;

  CV_FUNCNAME("icvReadCNNLayer");
  __BEGIN__;

  int n_input_planes, input_height, input_width;
  int n_output_planes, output_height, output_width;
  int learn_type, layer_type;
  float init_learn_rate;

  CV_CALL(n_input_planes  = cvReadIntByName( fs, node, "n_input_planes",  -1 ));
  CV_CALL(input_height    = cvReadIntByName( fs, node, "input_height",    -1 ));
  CV_CALL(input_width     = cvReadIntByName( fs, node, "input_width",     -1 ));
  CV_CALL(n_output_planes = cvReadIntByName( fs, node, "n_output_planes", -1 ));
  CV_CALL(output_height   = cvReadIntByName( fs, node, "output_height",   -1 ));
  CV_CALL(output_width    = cvReadIntByName( fs, node, "output_width",    -1 ));
  CV_CALL(layer_type      = cvReadIntByName( fs, node, "layer_type",      -1 ));

  CV_CALL(init_learn_rate = (float)cvReadRealByName( fs, node, "init_learn_rate", -1 ));
  CV_CALL(learn_type = cvReadIntByName( fs, node, "learn_rate_decrease_type", -1 ));
  CV_CALL(weights    = (CvMat*)cvReadByName( fs, node, "weights" ));

  if ( n_input_planes < 0  || input_height < 0  || input_width < 0 ||
       n_output_planes < 0 || output_height < 0 || output_width < 0 ||
       init_learn_rate < 0 || learn_type < 0 || layer_type < 0 || !weights ) {
    CV_ERROR( CV_StsParseError, "" );
  }

  // if ( layer_type == ICV_CNN_CONVOLUTION_LAYER ) {
  //   const int K = input_height - output_height + 1;
  //   if ( K <= 0 || K != input_width - output_width + 1 ) {
  //     CV_ERROR( CV_StsBadArg, "Invalid <K>" );
  //   }
  //   CV_CALL(connect_mask = (CvMat*)cvReadByName( fs, node, "connect_mask" ));
  //   if ( !connect_mask ) {
  //     CV_ERROR( CV_StsParseError, "Missing <connect mask>" );
  //   }
  //   CV_CALL(layer = cvCreateCNNConvolutionLayer( "", 0,
  //     n_input_planes, input_height, input_width, n_output_planes, K,
  //     init_learn_rate, learn_type, connect_mask, weights ));
  // } else if ( layer_type == ICV_CNN_SUBSAMPLING_LAYER ){
  //   const int sub_samp_scale = input_height/output_height;
  //   if ( sub_samp_scale <= 0 || sub_samp_scale != input_width/output_width ) {
  //     CV_ERROR( CV_StsBadArg, "Invalid <sub_samp_scale>" );
  //   }
  //   CV_CALL(layer = cvCreateCNNSubSamplingLayer( "", 0,
  //     n_input_planes, input_height, input_width, sub_samp_scale,
  //     init_learn_rate, learn_type, weights ));
  // } else if ( layer_type == ICV_CNN_FULLCONNECT_LAYER ){
  //   if ( input_height != 1  || input_width != 1 || output_height != 1 || output_width != 1 ) { 
  //     CV_ERROR( CV_StsBadArg, "" ); 
  //   }
  //   CV_CALL(layer = cvCreateCNNFullConnectLayer( "", 0, 0, n_input_planes, n_output_planes,
  //     init_learn_rate, learn_type, "tanh", weights ));
  // } else {
  //   CV_ERROR( CV_StsBadArg, "Invalid <layer_type>" );
  // }

  __END__;

  if ( cvGetErrStatus() < 0 && layer )
      layer->release( &layer );

  cvReleaseMat( &weights );
  cvReleaseMat( &connect_mask );

  return layer;
}

/****************************************************************************************/
static void icvWriteCNNLayer( CvFileStorage* fs, CvCNNLayer* layer )
{
  CV_FUNCNAME ("icvWriteCNNLayer");
  __BEGIN__;

  if ( !icvIsCNNLayer(layer) ) { CV_ERROR( CV_StsBadArg, "Invalid layer" ); }

  CV_CALL( cvStartWriteStruct( fs, NULL, CV_NODE_MAP, "opencv-ml-cnn-layer" ));

  CV_CALL(cvWriteInt( fs, "n_input_planes",  layer->n_input_planes ));
  CV_CALL(cvWriteInt( fs, "input_height",    layer->input_height ));
  CV_CALL(cvWriteInt( fs, "input_width",     layer->input_width ));
  CV_CALL(cvWriteInt( fs, "n_output_planes", layer->n_output_planes ));
  CV_CALL(cvWriteInt( fs, "output_height",   layer->output_height ));
  CV_CALL(cvWriteInt( fs, "output_width",    layer->output_width ));
  CV_CALL(cvWriteInt( fs, "learn_rate_decrease_type", layer->decay_type));
  CV_CALL(cvWriteReal( fs, "init_learn_rate", layer->init_learn_rate ));
  CV_CALL(cvWrite( fs, "weights", layer->weights ));

  if ( icvIsCNNConvolutionLayer( layer )){
    CvCNNConvolutionLayer* l = (CvCNNConvolutionLayer*)layer;
    CV_CALL(cvWriteInt( fs, "layer_type", ICV_CNN_CONVOLUTION_LAYER ));
    CV_CALL(cvWrite( fs, "connect_mask", l->connect_mask ));
  }else if ( icvIsCNNSubSamplingLayer( layer ) ){
    CvCNNSubSamplingLayer* l = (CvCNNSubSamplingLayer*)layer;
    CV_CALL(cvWriteInt( fs, "layer_type", ICV_CNN_SUBSAMPLING_LAYER ));
  }else if ( icvIsCNNFullConnectLayer( layer ) ){
    CvCNNFullConnectLayer* l = (CvCNNFullConnectLayer*)layer;
    CV_CALL(cvWriteInt( fs, "layer_type", ICV_CNN_FULLCONNECT_LAYER ));
  }else {
    CV_ERROR( CV_StsBadArg, "Invalid layer" );
  }

  CV_CALL( cvEndWriteStruct( fs )); //"opencv-ml-cnn-layer"

  __END__;
}

/****************************************************************************************/
static CvCNNetwork * icvCNNetworkRead( CvFileStorage * fs )
{
  CvCNNetwork * cnn = 0;
  CvCNNLayer * layer = 0;

  CV_FUNCNAME("icvReadCNNModel");
  __BEGIN__;

//  CvFileNode* node;
//  CvSeq* seq;
//  CvSeqReader reader;
//  int i;
//
//  CV_CALL(cnn = (CvCNNStatModel*)cvCreateCNNStatModel(
//    CV_STAT_MODEL_MAGIC_VAL|CV_CNN_MAGIC_VAL, sizeof(CvCNNStatModel)));
//
//  CV_CALL(cnn->etalons = (CvMat*)cvReadByName( fs, root_node, "etalons" ));
//  CV_CALL(cnn->cls_labels = (CvMat*)cvReadByName( fs, root_node, "cls_labels" ));
//
//  if ( !cnn->etalons || !cnn->cls_labels ) {
//    CV_ERROR( CV_StsParseError, "No <etalons> or <cls_labels> in CNN model" );
//  }
//
//  CV_CALL( node = cvGetFileNodeByName( fs, root_node, "network" ));
//  seq = node->data.seq;
//  if ( !CV_NODE_IS_SEQ(node->tag) ) { CV_ERROR( CV_StsBadArg, "" ); }
//
//  CV_CALL( cvStartReadSeq( seq, &reader, 0 ));
//  CV_CALL(layer = icvReadCNNLayer( fs, (CvFileNode*)reader.ptr ));
//  CV_CALL(cnn->network = cvCreateCNNetwork( layer ));
//
//  for ( i = 1; i < seq->total; i++ ) {
//    CV_NEXT_SEQ_ELEM( seq->elem_size, reader );
//    CV_CALL(layer = icvReadCNNLayer( fs, (CvFileNode*)reader.ptr ));
//    CV_CALL(cnn->network->add_layer( cnn->network, layer ));
//  }
//
  __END__;

  // if ( cvGetErrStatus() < 0 ) {
  //   if ( cnn ) { cnn->release( (CvCNNStatModel**)&cnn ); }
  //   if ( layer ) { layer->release( &layer ); }
  // }
  return cnn;
}

/****************************************************************************************/
// static void icvWriteCNNModel( CvFileStorage* fs, const char* name, 
//                               const void* struct_ptr, CvAttrList attr)
static void icvCNNetworkWrite( CvCNNetwork * struct_ptr, CvFileStorage * fs )

{
  CV_FUNCNAME ("icvWriteCNNetwork");
  __BEGIN__;
//
//  CvCNNStatModel* cnn = (CvCNNStatModel*)struct_ptr;
//  int n_layers, i;
//  CvCNNLayer* layer;
//
//  if ( !CV_IS_CNN(cnn) ) { CV_ERROR( CV_StsBadArg, "Invalid pointer" ); }
//
//  n_layers = cnn->network->n_layers;
//
//  CV_CALL( cvStartWriteStruct( fs, name, CV_NODE_MAP, CV_TYPE_NAME_ML_CNN ));
//
//  CV_CALL(cvWrite( fs, "etalons", cnn->etalons ));
//  CV_CALL(cvWrite( fs, "cls_labels", cnn->cls_labels ));
//
//  CV_CALL( cvStartWriteStruct( fs, "network", CV_NODE_SEQ ));
//
//  layer = cnn->network->first_layer;
//  for ( i = 0; i < n_layers && layer; i++, layer = layer->next_layer ) {
//    CV_CALL(icvWriteCNNLayer( fs, layer ));
//  }
//  if ( i < n_layers || layer ) { CV_ERROR( CV_StsBadArg, "Invalid network" ); }
//
//  CV_CALL( cvEndWriteStruct( fs )); //"network"
//  CV_CALL( cvEndWriteStruct( fs )); //"opencv-ml-cnn"
//
  __END__;
}

// static int icvRegisterCNNStatModelType()
// {
//   CvTypeInfo info;
//   info.header_size = sizeof( info );
//   info.is_instance = icvIsCNNModel;
//   info.release = icvReleaseCNNModel;
//   info.read = icvCNNetworkRead;
//   info.write = icvCNNetworkWrite;
//   info.clone = NULL;
//   info.type_name = CV_TYPE_NAME_ML_CNN;
//   cvRegisterType( &info );
//   return 1;
// } // End of icvRegisterCNNStatModelType
// static int cnn = icvRegisterCNNStatModelType();

// End of file