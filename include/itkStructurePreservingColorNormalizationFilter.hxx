/*=========================================================================
 *
 *  Copyright NumFOCUS!!!
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef itkStructurePreservingColorNormalizationFilter_hxx
#define itkStructurePreservingColorNormalizationFilter_hxx

#include "itkStructurePreservingColorNormalizationFilter.h"
#include "itkRGBPixel.h"
#include <numeric>

namespace itk
{

template< typename TInputImage, typename TOutputImage >
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::StructurePreservingColorNormalizationFilter()
{}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::PrintSelf( std::ostream & os, Indent indent ) const
{
  Superclass::PrintSelf( os, indent );
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::GenerateInputRequestedRegion()
{
  // { std::ostringstream mesg; mesg << "Entering GenerateInputRequestedRegion" << std::endl; std::cout << mesg.str(); }

  // Call the superclass' implementation of this method
  Superclass::GenerateInputRequestedRegion();

  // Get pointers to the input and output
  InputImageType *inputPtr = const_cast< InputImageType * >( this->GetInput( 0 ) );
  InputImageType *referPtr = const_cast< InputImageType * >( this->GetInput( 1 ) );

  if( inputPtr != nullptr )
    {
    inputPtr->SetRequestedRegionToLargestPossibleRegion();
    }

  if( referPtr != nullptr )
    {
    referPtr->SetRequestedRegionToLargestPossibleRegion();
    }
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::BeforeThreadedGenerateData()
{
  // { std::ostringstream mesg; mesg << "Entering BeforeThreadedGenerateData" << std::endl; std::cout << mesg.str(); }

  // Call the superclass' implementation of this method
  Superclass::BeforeThreadedGenerateData();

  // Find input, refer, output, and make iterators for them.
  const InputImageType * const inputPtr = this->GetInput( 0 ); // image to be normalized
  const InputImageType * const referPtr = this->GetInput( 1 ); // reference image
  // For each input, make sure that it was supplied, or that we have
  // it cached already.
  itkAssertOrThrowMacro( inputPtr != nullptr || m_inputPtr != nullptr, "An image to be normalized needs to be supplied as input image #0" );
  itkAssertOrThrowMacro( referPtr != nullptr || m_referPtr != nullptr, "An reference image needs to be supplied as input image #1" );

  // For each input, if there is a supplied image and it is different
  // from what we have cached then compute stuff and cache the
  // results.
  if( inputPtr != nullptr && ( inputPtr != m_inputPtr || inputPtr->GetTimeStamp() != m_inputTimeStamp ) )
    {
    InputRegionConstIterator inIter {inputPtr, inputPtr->GetRequestedRegion()};
    CalcMatrixType inputV;
    InputPixelType inputUnstainedPixel;
    if( this->ImageToNMF( inIter, inputV, m_inputW, m_inputH, inputUnstainedPixel ) == 0 )
      {
      m_inputPtr = inputPtr;
      m_inputTimeStamp = inputPtr->GetTimeStamp();
      }
    else
      {
      // we failed
      m_inputPtr = nullptr;
      }
    }

  if( referPtr != nullptr && ( referPtr != m_referPtr || referPtr->GetTimeStamp() != m_referTimeStamp ) )
    {
    InputRegionConstIterator refIter {referPtr, referPtr->GetRequestedRegion()};
    CalcMatrixType referV;
    CalcMatrixType referW;
    if( this->ImageToNMF( refIter, referV, referW, m_referH, m_referUnstainedPixel ) == 0)
      {
      m_referPtr = referPtr;
      m_referTimeStamp = referPtr->GetTimeStamp();
      }
    else
      {
      // we failed
      m_referPtr = nullptr;
      }
    }
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::DynamicThreadedGenerateData( const OutputRegionType & outputRegion )
{
  // { std::ostringstream mesg; mesg << "Entering DynamicThreadedGenerateData" << std::endl; std::cout << mesg.str(); }

  OutputImageType * const outputPtr = this->GetOutput();
  itkAssertOrThrowMacro( outputPtr != nullptr, "An output image needs to be supplied" )
  itkAssertOrThrowMacro( m_inputPtr != nullptr, "The image to be normalized could not be processed" )
  itkAssertOrThrowMacro( m_referPtr != nullptr, "The reference image could not be processed" )
  OutputRegionIterator outIter {outputPtr, outputRegion};

  this->NMFsToImage( m_inputW, m_inputH, m_referH, m_referUnstainedPixel, outIter );
}


template< typename TInputImage, typename TOutputImage >
int
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::ImageToNMF( InputRegionConstIterator &iter, CalcMatrixType &matrixV, CalcMatrixType &matrixW, CalcMatrixType &matrixH, InputPixelType &unstainedPixel ) const
{
  // { std::ostringstream mesg; mesg << "Entering ImageToNMF" << std::endl; std::cout << mesg.str(); }

  const InputSizeType size {iter.GetRegion().GetSize()};
  const unsigned int numberOfPixels = std::accumulate( size.begin(), size.end(), 1, std::multiplies< InputSizeValueType >() );
  // To maintain locality of memory references, we are using
  // numberOfPixels as the number of rows rather than as the number of
  // columns.  With V=WH, as is standard in non-negative matrix
  // factorization, our matrices switch names and are transposed with
  // respect to the Vahadane article.  In particular, our W is a tall
  // matrix and our H is a fairly compact matrix.
  matrixV = CalcMatrixType {numberOfPixels, InputImageLength};
  matrixW = CalcMatrixType {numberOfPixels, NumberOfStains};
  matrixH = CalcMatrixType {NumberOfStains, InputImageLength};

  // A vector that has a 1 for each row of matrixV.
  const CalcVectorType firstOnes {matrixV.rows(), 1.0};

  // Find distinguishers to get a very good starting point for the subsequent
  // generic NMF algorithm.
  CalcMatrixType distinguishers;
  this->ImageToMatrix( iter, matrixV );
  this->MatrixToDistinguishers( matrixV, distinguishers );

  // Use the distinguishers as seeds to the non-negative matrix
  // factorization.  The published SPCN algorithm uses a Euclidean
  // penalty function, so we will hard code its use here.
  if( this->DistinguishersToNMFSeeds( distinguishers, unstainedPixel, matrixV, matrixW, matrixH ) != 0 )
    {
    return 1;                   // we failed.
    }
  // { std::ostringstream mesg; mesg << "Before VirtanenEuclidean: (log) matrixH = " << matrixH << std::end; }
  // { std::ostringstream mesg; mesg << "Before VirtanenEuclidean: (log) matrixW = " << matrixW << std::end; }
  // this->VirtanenEuclidean( matrixV, matrixW, matrixH );

  // Round off values in the response, so that numbers of order 1e-16
  // are set to zero.
  const CalcElementType maxW = matrixW.array_inf_norm() * 15;
  matrixW += maxW;
  matrixW -= maxW;
  const CalcElementType maxH = matrixH.array_inf_norm() * 15;
  matrixH += maxH;
  matrixH -= maxH;

  // { std::ostringstream mesg; mesg << "ImageToNMF: (log) matrixH = " << matrixH << std::end; }
  // { std::ostringstream mesg; mesg << "ImageToNMF: (log) matrixW = " << matrixW << std::end; }
  return 0;
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::ImageToMatrix( InputRegionConstIterator &iter, CalcMatrixType &matrixV ) const
{
  // { std::ostringstream mesg; mesg << "Entering ImageToMatrix" << std::endl; std::cout << mesg.str(); }

  int pixelIndex {0};
  for( iter.GoToBegin(); !iter.IsAtEnd(); ++iter, ++pixelIndex )
    {
    InputPixelType pixelValue = iter.Get();
    for( int color {0}; color < InputImageLength; ++color )
      {
      matrixV.put( pixelIndex, color, pixelValue[color] );
      }
    }
  // We do not want trouble with a value near zero (when we take its
  // logarithm) so we add a little to each value now.
  const CalcElementType nearZero {matrixV.array_inf_norm() * epsilon};
  matrixV += nearZero;
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::MatrixToDistinguishers( const CalcMatrixType &matrixV, CalcMatrixType &distinguishers ) const
{
  // { std::ostringstream mesg; mesg << "Entering MatrixToDistinguishers" << std::endl; std::cout << mesg.str(); }

  // Keep only pixels that are bright enough.
  const CalcMatrixType brightV {this->MatrixToBrightPartOfMatrix( matrixV )};

  // Useful vectors
  const CalcVectorType firstOnes {brightV.rows(), 1.0};
  const CalcVectorType lastOnes {brightV.cols(), 1.0};

  const CalcMatrixType normVStart {brightV};

  // We will store the row (pixel) index of each distinguishing pixel
  // in firstPassDistinguisherIndices.
  std::array< int, NumberOfStains+1 > firstPassDistinguisherIndices {-1};
  unsigned int numberOfDistinguishers {0};
  this->FirstPassDistinguishers( normVStart, firstPassDistinguisherIndices, numberOfDistinguishers );

  // Each row of secondPassDistinguisherColors is the vector of color
  // values for a distinguisher.
  CalcMatrixType secondPassDistinguisherColors {numberOfDistinguishers, brightV.cols()};
  this->SecondPassDistinguishers( normVStart, firstPassDistinguisherIndices, numberOfDistinguishers, brightV, secondPassDistinguisherColors );

  distinguishers = secondPassDistinguisherColors;
}


template< typename TInputImage, typename TOutputImage >
typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcMatrixType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::MatrixToBrightPartOfMatrix( const CalcMatrixType &matrixV ) const
{
  // { std::ostringstream mesg; mesg << "Entering MatrixToBrightPartOfMatrix" << std::endl; std::cout << mesg.str(); }

  // A useful vector that has a 1 for each column of matrixV.
  const CalcVectorType lastOnes {matrixV.cols(), 1.0};

  // We want only the brightest pixels.  Find the 80th percentile threshold.
  const CalcVectorType brightnessOriginal {matrixV * lastOnes};
  CalcVectorType brightnessOrdered {brightnessOriginal};
  const CalcElementType percentileLevel {0.80};
  int const quantilePosition {static_cast< int >( ( brightnessOrdered.size() - 1 ) * percentileLevel )};
  std::nth_element( brightnessOrdered.begin(), &brightnessOrdered[quantilePosition], brightnessOrdered.end() );
  const CalcElementType percentileThreshold {brightnessOrdered[quantilePosition]};
  // Find 70% of maximum brightness
  const CalcElementType percentageLevel {0.70};
  const CalcElementType percentageThreshold {percentageLevel * *std::max_element( brightnessOriginal.begin(), brightnessOriginal.end() )};

  // We will keep those pixels that pass at least one of the above
  // thresholds.
  const CalcElementType brightnessThreshold {std::min( percentileThreshold, percentageThreshold )};
  unsigned int numberOfRowsToKeep {0};
  for( int i {0} ; i < matrixV.rows(); ++i )
    {
    if( brightnessOriginal.get( i ) >= brightnessThreshold )
      {
      ++numberOfRowsToKeep;
      }
    }
  CalcMatrixType brightV {numberOfRowsToKeep, matrixV.cols()};
  numberOfRowsToKeep = 0;
  for( int i {0} ; i < matrixV.rows(); ++i )
    {
    if( brightnessOriginal.get( i ) >= brightnessThreshold )
      {
      brightV.set_row( numberOfRowsToKeep++, matrixV.get_row( i ) );
      }
    }
  return brightV;
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::FirstPassDistinguishers( const CalcMatrixType &normVStart, std::array< int, NumberOfStains+1 > &firstPassDistinguisherIndices, unsigned int &numberOfDistinguishers ) const
{
  // { std::ostringstream mesg; mesg << "Entering FirstPassDistinguishers" << std::endl; std::cout << mesg.str(); }

  // A useful vector that has a 1 for each column of normVStart.
  const CalcVectorType lastOnes {normVStart.cols(), 1.0};
  // A useful vector that has a 1 for each row of normVStart.
  const CalcVectorType firstOnes {normVStart.rows(), 1.0};

  CalcMatrixType normV {normVStart};
  numberOfDistinguishers = 0;
  bool needToRecenterMatrix = true;
  while( numberOfDistinguishers <= NumberOfStains )
    {
    // Find the next distinguishing row (pixel)
    firstPassDistinguisherIndices[numberOfDistinguishers] = this->MatrixToOneDistinguisher( normV, lastOnes );
    // If we found a distinguisher and we have not yet found
    // NumberOfStains+1 of them, then look for the next distinguisher.
    if( firstPassDistinguisherIndices[numberOfDistinguishers] >= 0 )
      {
      // We just found a distinguisher
      ++numberOfDistinguishers;
      if( numberOfDistinguishers <= NumberOfStains )
        {
        // Prepare to look for the next distinguisher
        if( needToRecenterMatrix )
          {
          normV = this->RecenterMatrix( normV, firstOnes, firstPassDistinguisherIndices[numberOfDistinguishers - 1] );
          needToRecenterMatrix = false;
          }
        else
          {
          normV = this->ProjectMatrix( normV, firstPassDistinguisherIndices[numberOfDistinguishers - 1] );
          }
        }
      }
    else
      {
      // We did not find another distinguisher.  There are no more.
      break;
      }
    }
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::SecondPassDistinguishers( const CalcMatrixType &normVStart, const std::array< int, NumberOfStains+1 > &firstPassDistinguisherIndices, const int numberOfDistinguishers,
  const CalcMatrixType &brightV, CalcMatrixType &secondPassDistinguisherColors ) const
{
  // { std::ostringstream mesg; mesg << "Entering SecondPassDistinguishers" << std::endl; std::cout << mesg.str(); }

  // A useful vector that has a 1 for each column of normVStart.
  const CalcVectorType lastOnes {normVStart.cols(), 1.0};
  // A useful vector that has a 1 for each row of normVStart.
  const CalcVectorType firstOnes {normVStart.rows(), 1.0};

  for( int distinguisher {0}; distinguisher < numberOfDistinguishers; ++distinguisher )
    {
    CalcMatrixType normV {normVStart};
    bool needToRecenterMatrix = true;
    for( int otherDistinguisher {0}; otherDistinguisher < numberOfDistinguishers; ++otherDistinguisher )
      {
      // skip if self
      if( otherDistinguisher != distinguisher )
        {
        if( needToRecenterMatrix )
          {
          normV = this->RecenterMatrix( normV, firstOnes, firstPassDistinguisherIndices[otherDistinguisher] );
          needToRecenterMatrix = false;
          }
        else
          {
          normV = this->ProjectMatrix( normV, firstPassDistinguisherIndices[otherDistinguisher] );
          }
        }
      }
    // We have sent all distinguishers except self to the origin.
    // Whatever is far from the origin in the same direction as self
    // is a good replacement for self.  We will take an average among
    // those that are at least 80% as far as the best.  (Note that
    // self could still be best, but not always.)
    const CalcVectorType dotProducts {normV * normV.get_row( firstPassDistinguisherIndices[distinguisher] )};
    const CalcElementType threshold {*std::max_element( dotProducts.begin(), dotProducts.end() ) * 999 / 1000};
    CalcVectorType cumulative {brightV.cols(), 0.0};
    int numberOfContributions {0};
    for( int row {0}; row < dotProducts.size(); ++row )
      {
      if( dotProducts[row] >= threshold )
        {
        cumulative += brightV.get_row( row );
        ++numberOfContributions;
        }
      }
    // { std::ostringstream mesg; mesg << "SecondPassDistinguishers::numberOfContributions = " << numberOfContributions << std::end; }
    secondPassDistinguisherColors.set_row( distinguisher, cumulative / numberOfContributions );
    }
  // { std::ostringstream mesg; mesg << "secondPassDistinguisherColors = " << secondPassDistinguisherColors << std::end; }
}


template< typename TInputImage, typename TOutputImage >
int
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::MatrixToOneDistinguisher( const CalcMatrixType &normV, const CalcVectorType &lastOnes ) const
{
  const CalcVectorType lengths2 = element_product( normV, normV ) * lastOnes;
  const CalcElementType * const result {std::max_element( lengths2.begin(), lengths2.end() )};
  if( *result > epsilon2 )
    {
    return std::distance( lengths2.begin(), result );
    }
  else
    {
    return -1;                // Nothing left to find
    }
}


template< typename TInputImage, typename TOutputImage >
typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcMatrixType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::RecenterMatrix( const CalcMatrixType &normV, const CalcVectorType &firstOnes, const int row ) const
{
  return normV - outer_product( firstOnes, normV.get_row( row ) );
}


template< typename TInputImage, typename TOutputImage >
typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcMatrixType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::ProjectMatrix( const CalcMatrixType &normV, const int row ) const
{
  const CalcVectorType rowValue {normV.get_row( row )};
  const CalcElementType squared_magnitude = dot_product( rowValue, rowValue );
  return normV - outer_product( ( normV * rowValue ), rowValue / squared_magnitude );
}


template< typename TInputImage, typename TOutputImage >
int
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::DistinguishersToNMFSeeds( const CalcMatrixType &distinguishers, InputPixelType &unstainedPixel, CalcMatrixType &matrixV, CalcMatrixType &matrixW, CalcMatrixType &matrixH ) const
{
  // { std::ostringstream mesg; mesg << "Entering DistinguishersToNMFSeeds" << std::endl; std::cout << mesg.str(); }

  const CalcVectorType firstOnes {matrixW.rows(), 1.0};
  const CalcVectorType midOnes {matrixH.rows(), 1.0};
  const CalcVectorType lastOnes {matrixV.cols(), 1.0};

  int unstainedIndex;
  int hematoxylinIndex;
  int eosinIndex;
  this->DistinguishersToColors( distinguishers, unstainedIndex, hematoxylinIndex, eosinIndex );

  // If the indices unstainedIndex, hematoxylinIndex, and eosinIndex
  // are distinct then we choose a smart starting place for the
  // generic NMF algorithm.  Otherwise, we go with a guess that is
  // reasonable.
  if( unstainedIndex != hematoxylinIndex && unstainedIndex != eosinIndex && hematoxylinIndex != eosinIndex )
    {
    const CalcVectorType unstainedCalcPixel {distinguishers.get_row( unstainedIndex )};
    for( int color {0}; color < InputImageLength; ++ color )
      {
      unstainedPixel[color] = unstainedCalcPixel[color]; // return value
      }
    const CalcVectorType logUnstained {unstainedCalcPixel.apply( std::log )};
    const CalcVectorType logHematoxylin {logUnstained - distinguishers.get_row( hematoxylinIndex ).apply( std::log )};
    const CalcVectorType logEosin {logUnstained - distinguishers.get_row( eosinIndex ).apply( std::log )};
    // Set rows of matrixH to reflect hematoxylin and eosin.
    matrixH.set_row( 0, logHematoxylin );
    matrixH.set_row( 1, logEosin );
    // Convert matrixV to be the exponents of decay from the unstained
    // pixel.
    matrixV = outer_product( firstOnes, logUnstained ) - matrixV.apply( std::log );
    }
  else
    {
    return 1;                   // we failed
    }

  // Make sure that matrixV is non-negative.
  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  matrixV = matrixV.apply( clip );

  // Make sure that each row of matrixH has unit magnitude, and each
  // element of matrixH is sufficiently non-negative.
  matrixH = static_cast< CalcDiagMatrixType >( ( element_product( matrixH, matrixH ) * lastOnes ).apply( std::sqrt ) ).invert_in_place() * matrixH;
  matrixH = matrixH.apply( clip );

  // Use an approximate inverse to matrixH to get an intial value of
  // matrixW, and make sure that matrixW is sufficiently non-negative.
  const CalcMatrixType kernel {vnl_matrix_inverse< CalcElementType >( matrixH * matrixH.transpose() ).as_matrix()};
  // Do we really want lambda here?!!!
  matrixW = ( matrixV * ( matrixH.transpose() * kernel ) ) - outer_product( ( lambda * firstOnes ), ( midOnes * kernel ) );
  matrixW = matrixW.apply( clip );
  return 0;
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::DistinguishersToColors( CalcMatrixType const &distinguishers, int &unstainedIndex, int &hematoxylinIndex, int &eosinIndex ) const
{
  // { std::ostringstream mesg; mesg << "Entering DistinguishersToColors" << std::endl; std::cout << mesg.str(); }

  // Figure out which, distinguishers are unstained (highest
  // brightness), hematoxylin (suppresses red), and eosin (suppresses
  // green).
  const CalcVectorType lastOnes {distinguishers.cols(), 1.0};
  const CalcVectorType lengths2 {element_product( distinguishers, distinguishers ) * lastOnes};
  const typename CalcVectorType::const_iterator unstainedIterator {std::max_element( lengths2.begin(), lengths2.end() )};
  unstainedIndex = std::distance( lengths2.begin(), unstainedIterator );
  // For typename RGBPixel, red is suppressed by hematoxylin and is
  // color 0; green is suppressed by eosin and is color 1.  What if
  // InputPixelType is some other multi-color type ... how would we
  // find a color number that is expected to be suppressed by
  // hematoxylin and a color number that is expected to be suppressed
  // by eosin?!!!
  const CalcVectorType redValues {distinguishers.get_column( 0 )};
  const typename CalcVectorType::const_iterator hematoxylinIterator {std::min_element( redValues.begin(), redValues.end() )};
  hematoxylinIndex = std::distance( redValues.begin(), hematoxylinIterator );
  const CalcVectorType greenValues {distinguishers.get_column( 1 )};
  const typename CalcVectorType::const_iterator eosinIterator {std::min_element( greenValues.begin(), greenValues.end() )};
  eosinIndex = std::distance( greenValues.begin(), eosinIterator );
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::VirtanenEuclidean( const CalcMatrixType &matrixV, CalcMatrixType &matrixW, CalcMatrixType &matrixH ) const
{
  // { std::ostringstream mesg; mesg << "Entering VirtanenEuclidean" << std::endl; std::cout << mesg.str(); }

  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  const CalcVectorType lastOnes {matrixV.cols(), 1.0};
  // Apply Virtanen's algorithm to iteratively improve matrixW and
  // matrixH.  Note that parentheses optimize the order of matrix
  // chain multiplications and affect the speed of this method.
  CalcMatrixType previousMatrixW {matrixW};
  unsigned int iter = 0;
  for( ; iter < maxNumberOfIterations; ++iter )
    {
    // Lasso term "lambda" insertion is possibly in a novel way.
    matrixW = element_product( matrixW, element_quotient( ( matrixV * matrixH.transpose() - lambda ).apply( clip ) + epsilon2, matrixW * ( matrixH * matrixH.transpose() ) + epsilon2 ) );
    matrixH = element_product( matrixH, element_quotient( matrixW.transpose() * matrixV + epsilon2, ( matrixW.transpose() * matrixW ) * matrixH + epsilon2 ) );
    // In lieu of rigorous Lagrange multipliers, renormalize rows of
    // matrixH to have unit magnitude.
    matrixH = static_cast< CalcDiagMatrixType >( ( element_product( matrixH, matrixH ) * lastOnes ).apply( std::sqrt ) ).invert_in_place() * matrixH;
    if( ( iter & 15 ) == 15 )
      {
      if( ( matrixW - previousMatrixW ).array_inf_norm() < biggerEpsilon )
        {
        break;
        }
      previousMatrixW = matrixW;
      }
    }
  // { std::ostringstream mesg; mesg << "Number of iterations = " << iter << std::end; }
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::VirtanenKLDivergence( const CalcMatrixType &matrixV, CalcMatrixType &matrixW, CalcMatrixType &matrixH ) const
{
  // If this method is going to get used, we may need to incorporate
  // the Lasso penalty lambda for matrixW and incorporate the Lagrange
  // multipliers to make each row of matrixH have magnitude 1.0.

  // { std::ostringstream mesg; mesg << "Entering VirtanenKLDivergence" << std::endl; std::cout << mesg.str(); }

  // Apply Virtanen's algorithm to iteratively improve matrixW and
  // matrixH.
  const CalcVectorType firstOnes {matrixV.rows(), 1.0};
  const CalcVectorType lastOnes {matrixV.cols(), 1.0};
  CalcMatrixType previousMatrixW {matrixW};
  for( unsigned int iter = 0; iter < maxNumberOfIterations; ++iter )
    {
    matrixW = element_product( matrixW, element_quotient( element_quotient( matrixV + epsilon2, matrixW * matrixH + epsilon2 ) * matrixH.transpose() + epsilon2,
        outer_product( firstOnes, lastOnes * matrixH.transpose() ) + epsilon2 ) );
    matrixH = element_product( matrixH, element_quotient( matrixW.transpose() * element_quotient( matrixV + epsilon2, matrixW * matrixH + epsilon2 ) + epsilon2,
        outer_product( matrixW.transpose() * firstOnes, lastOnes ) + epsilon2 ) );
    if( iter & 15 == 15 )
      {
      if( ( matrixW - previousMatrixW ).array_inf_norm() < biggerEpsilon )
        break;
      previousMatrixW = matrixW;
      }
    }
  // { std::ostringstream mesg; mesg << "final matrixH = " << matrixH << std::endl << "final matrixW = " << matrixW << std::endl; std::cout << mesg.str(); }
}


template< typename TInputImage, typename TOutputImage >
void
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::NMFsToImage( const CalcMatrixType &inputW, const CalcMatrixType &inputH, const CalcMatrixType &referH, const InputPixelType &referUnstained, OutputRegionIterator &out ) const
{
  // { std::ostringstream mesg; mesg << "Entering NMFsToImage" << std::endl; std::cout << mesg.str(); }

  // We will set normalizedH to referH and then manipulate the former.
  CalcMatrixType normalizedH {referH};

  if( vnl_determinant( inputH * normalizedH.transpose() ) < CalcElementType( 0 ) )
    {
    // Somehow the hematoxylin and eosin rows got swaped in one of the
    // input image or reference image.  Flip them back in normalizedH to
    // get them in synch.
    static_assert( NumberOfStains == 2, "StructurePreservingColorNormalizationFilter current implementation assumes exactly two stains" );
    normalizedH.set_row( 0, referH.get_row( 1 ) );
    normalizedH.set_row( 1, referH.get_row( 0 ) );
    }

  // Correct for any scaling difference between normalizedH and inputH.
  const CalcVectorType lastOnes {normalizedH.cols(), CalcElementType( 1 )};
  normalizedH = {static_cast< CalcDiagMatrixType >( element_quotient( element_product( inputH, inputH ) * lastOnes + epsilon2,
        element_product( normalizedH, normalizedH ) * lastOnes +epsilon2 ).apply( std::sqrt ) ) * normalizedH};

  // Use the reference image's stain colors and input image's stain
  // levels to compute what the input image would look like with the
  // reference images colors.
  CalcMatrixType newV = inputW * normalizedH;

  // Exponentiate and use as a divisor of the color of an unstained pixel.
  const unsigned int numberOfRows {newV.rows()};
  const unsigned int numberOfCols {newV.cols()};
  for( unsigned int row {0}; row < numberOfRows; ++row )
    {
    for( unsigned int col {0}; col < numberOfCols; ++col )
      {
      newV.put( row, col, referUnstained[col] / exp( newV.get( row, col ) ) );
      }
    }

  // Write the pixel values into the output region.
  InputRegionConstIterator in {m_inputPtr, m_inputPtr->GetRequestedRegion()};

  in.GoToBegin();               // for indexing of input image
  int pixelIndex {0};           // for indexing newV.
  OutputPixelType pixelValue;
  for( out.GoToBegin(); !out.IsAtEnd(); ++out, ++in, ++pixelIndex )
    {
    // Find input index that matches this output index.
    while ( in.GetIndex() != out.GetIndex() )
      {
      ++in;
      ++pixelIndex;
      }
    for( int color {0}; color < InputImageLength; ++color )
      {
      pixelValue[color] = newV.get( pixelIndex, color );
      }
    out.Set( pixelValue );
    }
}


// biggerEpsilon, epsilon, epsilon2, and lambda are explicitly defined
// here, even though they are declared and initialized as static
// constexpr members, because they are passed by reference in some
// versions of the implementation, and that can get some compilers to
// complain.
template< typename TInputImage, typename TOutputImage >
const typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::biggerEpsilon;

template< typename TInputImage, typename TOutputImage >
const typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::epsilon;

template< typename TInputImage, typename TOutputImage >
const typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::epsilon2;

template< typename TInputImage, typename TOutputImage >
const typename StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TInputImage, TOutputImage >
::lambda;

} // end namespace itk

#endif // itkStructurePreservingColorNormalizationFilter_hxx
