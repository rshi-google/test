/************************************************************************************

Filename    :   PanoBrowser.cpp
Content     :
Created     :
Authors     :

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

This source code is licensed under the BSD-style license found in the
LICENSE file in the Oculus360Photos/ directory. An additional grant
of patent rights can be found in the PATENTS file in the same directory.

*************************************************************************************/

#include "PanoBrowser.h"
#include "Kernel/OVR_String_Utils.h"
#include "Oculus360Photos.h"
#include "ImageData.h"
#include "VrApi/VrLocale.h"
#include "BitmapFont.h"
#include "VRMenu/GuiSys.h"

namespace OVR
{

static const int FAVORITES_FOLDER_INDEX = 0;


// Takes a <name_nz.jpg file and adds half of the _px and _nx images
// to make a 2:1 aspect image for a thumbnail with the images reversed
// as needed to make it look "forward"
//
// Note that this won't be an actual wide-angle projection, but it is at least continuous
// and somewhat reasonable looking.
unsigned char * CubeMapVista( const char * nzName, float const ratio, int & width, int & height )
{
	int	faceWidth = 0;
	int	faceHeight = 0;
	int	faceWidth2 = 0;
	int	faceHeight2 = 0;
	int	faceWidth3 = 0;
	int	faceHeight3 = 0;

	width = height = 0;

	unsigned char * nzData = NULL;
	unsigned char * pxData = NULL;
	unsigned char * nxData = NULL;
	unsigned char * combined = NULL;

	// make _nx.jpg and _px.jpg from provided _nz.jpg name
	const int len = strlen( nzName );
	if ( len < 6 )
	{
		return NULL;
	}
	StringBuffer	pxName( nzName );
	pxName[ len - 6 ] = 'p';
	pxName[ len - 5 ] = 'x';
	StringBuffer	nxName( nzName );
	nxName[ len - 5 ] = 'x';

	nzData = TurboJpegLoadFromFile( nzName, &faceWidth, &faceHeight );
	pxData = TurboJpegLoadFromFile( pxName, &faceWidth2, &faceHeight2 );
	nxData = TurboJpegLoadFromFile( nxName, &faceWidth3, &faceHeight3 );

	if ( nzData && pxData && nxData && ( faceWidth & 1 ) == 0 && faceWidth == faceHeight
		&& faceWidth2 == faceWidth && faceHeight2 == faceHeight
		&& faceWidth3 == faceWidth && faceHeight3 == faceHeight )
	{
		// find the total width and make sure it's an even number
		width = ( ( int )( floor( faceWidth * ratio ) ) >> 1 ) << 1;
		// subract the width of the center face, and split that into two halves
		int const half = ( width - faceWidth ) >> 1;
		height = faceHeight;
		combined = ( unsigned char * )malloc( width * height * 4 );
		for ( int y = 0; y < height; y++ )
		{
			int * dest = ( int * )combined + y * width;
			int * nz = ( int * )nzData + y * faceWidth;
			int * px = ( int * )pxData + y * faceWidth;
			int * nx = ( int * )nxData + y * faceWidth;
			for ( int x = 0; x < half; x++ )
			{
				dest[ x ] = nx[ half - 1 - x ];
				dest[ half + faceWidth + x ] = px[ faceWidth - 1 - x ];
			}
			for ( int x = 0; x < faceWidth; x++ )
			{
				dest[ half + x ] = nz[ faceWidth - 1 - x ];
			}
		}
	}

	if ( nzData )
	{
		free( nzData );
	}
	if ( nxData )
	{
		free( nxData );
	}
	if ( pxData )
	{
		free( pxData );
	}

	return combined;
}

PanoBrowser * PanoBrowser::Create(
	App *					app,
	const Array<String> & 	searchPaths,
	OvrMetaData &			metaData,
	unsigned				thumbWidth,
	float					horizontalPadding,
	unsigned				thumbHeight,
	float					verticalPadding,
	unsigned 				numSwipePanels,
	float					swipeRadius )
{
	return new PanoBrowser(
		app,
		searchPaths,
		metaData,
		static_cast< float >( thumbWidth + horizontalPadding ),
		static_cast< float >( thumbHeight + verticalPadding ),
		swipeRadius,
		numSwipePanels,
		thumbWidth,
		thumbHeight );
}

void PanoBrowser::OnPanelActivated( OvrMetaDatum * panelData )
{
	Oculus360Photos * photos = ( Oculus360Photos * )AppPtr->GetAppInterface();
	OVR_ASSERT( photos );
	photos->OnPanoActivated( panelData );
}

OvrMetaDatum * PanoBrowser::NextFileInDirectory( const int step )
{
	// if currently browsing favorites, handle this here
	if ( GetActiveFolderIndex() == FAVORITES_FOLDER_INDEX )
	{
		const int numFavorites = FavoritesBuffer.GetSizeI();
		// find the current
		int nextPanelIndex = -1;
		Oculus360Photos * photos = ( Oculus360Photos * )AppPtr->GetAppInterface();
		OVR_ASSERT( photos );
		for ( nextPanelIndex = 0; nextPanelIndex < numFavorites; ++nextPanelIndex )
		{
			const Favorite & favorite = FavoritesBuffer.At( nextPanelIndex );
			if ( photos->GetActivePano() == favorite.Data )
			{
				break;
			}
		}
		OvrMetaDatum * panoData = NULL;
		for ( int count = 0; count < numFavorites; ++count )
		{
			nextPanelIndex += step;
			if ( nextPanelIndex >= numFavorites )
			{
				nextPanelIndex = 0;
			}
			else if ( nextPanelIndex < 0 )
			{
				nextPanelIndex = numFavorites - 1;
			}

			if ( FavoritesBuffer.At( nextPanelIndex ).IsFavorite )
			{
				panoData = FavoritesBuffer.At( nextPanelIndex ).Data;
				break;
			}
		}

		if ( panoData )
		{
			SetCategoryRotation( GetActiveFolderIndex(), nextPanelIndex );
		}

		return panoData;
	}
	else // otherwise pass it to base
	{
		return OvrFolderBrowser::NextFileInDirectory( step );
	}
}

// When browser opens - load in whatever is in local favoritebuffer to favorites folder
void PanoBrowser::OnBrowserOpen()
{
	Array< OvrMetaDatum * > favoriteData;
	for ( int i = 0; i < FavoritesBuffer.GetSizeI(); ++i )
	{
		const Favorite & favorite = FavoritesBuffer.At( i );
		if ( favorite.IsFavorite )
		{
			favoriteData.PushBack( favorite.Data );
		}
	}

	if ( BufferDirty )
	{
		RebuildFolder( FAVORITES_FOLDER_INDEX, favoriteData );
		BufferDirty = false;
	}

	if ( GetActiveFolderIndex() == FAVORITES_FOLDER_INDEX )
	{
		// Do we have any favorites at all?
		bool haveAnyFavorite = false;
		for ( int i = 0; i < FavoritesBuffer.GetSizeI(); ++i )
		{
			const Favorite & favorite = FavoritesBuffer.At( i );
			if ( favorite.IsFavorite )
			{
				haveAnyFavorite = true;
				break;
			}
		}

		// Favorites is empty and active folder - hide it in FolderBrowser by scrolling down
		if ( !haveAnyFavorite )
		{
			LOG( "PanoBrowser::AddToFavorites setting OvrFolderBrowser::MOVE_ROOT_DOWN" );
			SetRootAdjust( OvrFolderBrowser::MOVE_ROOT_UP );
		}
	}
}

// Reload with what's currently in favorites folder in FolderBrowser
void PanoBrowser::ReloadFavoritesBuffer()
{
	FavoritesBuffer.Clear();
	Array< OvrMetaDatum * > favoriteData;
	GetFolderData( FAVORITES_FOLDER_INDEX, favoriteData );
	for ( int i = 0; i < favoriteData.GetSizeI(); ++i )
	{
		Favorite favorite;
		favorite.Data = favoriteData.At( i );
		favorite.IsFavorite = true;
		FavoritesBuffer.PushBack( favorite );
	}
}

// Add a panel to an existing folder
void PanoBrowser::AddToFavorites( OvrMetaDatum * const panoData )
{
	// Check if already in favorites
	for ( int i = 0; i < FavoritesBuffer.GetSizeI(); ++i )
	{
		Favorite & favorite = FavoritesBuffer.At( i );
		if ( panoData == favorite.Data )
		{
			// already in favorites, so make sure it's marked as a favorite
			if ( !favorite.IsFavorite )
			{
				favorite.IsFavorite = true;
				BufferDirty = true;
			}
			return;
		}
	}

	// Not found in the buffer so add it
	Favorite favorite;
	favorite.Data = panoData;
	favorite.IsFavorite = true;

	FavoritesBuffer.PushBack( favorite );
	BufferDirty = true;
}

// Remove panel from an existing folder
void PanoBrowser::RemoveFromFavorites( OvrMetaDatum * const panoData )
{
	// First check if in fav buffer, and if so mark it as not a favorite
	for ( int i = 0; i < FavoritesBuffer.GetSizeI(); ++i )
	{
		Favorite & favorite = FavoritesBuffer.At( i );
		if ( panoData == favorite.Data )
		{
			favorite.IsFavorite = false;
			BufferDirty = true;
			break;
		}
	}
}

int PanoBrowser::GetNumPanosInActive() const
{
	const int activeFolderIndex = GetActiveFolderIndex();
	if ( activeFolderIndex == FAVORITES_FOLDER_INDEX )
	{
		int numFavs = 0;
		for ( int i = 0; i < FavoritesBuffer.GetSizeI(); ++i )
		{
			if ( FavoritesBuffer.At( i ).IsFavorite )
			{
				++numFavs;
			}
		}
		return numFavs;
	}
	const OvrFolderBrowser::Folder & folder = GetFolder( activeFolderIndex );
	return folder.Panels.GetSizeI();
}


unsigned char * PanoBrowser::CreateThumbnail( const char * filename, int & outW, int & outH )
{
	int		width = 0;
	int		height = 0;
	unsigned char * data = NULL;

	if ( strstr( filename, "_nz.jpg" ) )
	{
		data = CubeMapVista( filename, 1.6f, width, height );
	}
	else
	{
		data = TurboJpegLoadFromFile( filename, &width, &height );
	}

	if ( !data )
	{
		return NULL;
	}

	outW = GetThumbWidth();
	outH = GetThumbHeight();

	unsigned char * outBuffer = NULL;

	// For most panos - this is a simple and fast sampling into the typically much larger image
	if ( width >= outW && height >= outH )
	{
		outBuffer = ( unsigned char * )malloc( outW * outW * 4 );

		const int xJump = width / outW;
		const int yJump = height / outH;

		for ( int y = 0; y < outH; y++ )
		{
			for ( int x = 0; x < outW; x++ )
			{
				for ( int c = 0; c < 4; c++ )
				{
					outBuffer[ ( y * outW + x ) * 4 + c ] = data[ ( y * yJump * width + ( x * xJump ) ) * 4 + c ];
				}
			}
		}
	}
	else // otherwise we let ScaleImageRGBA upscale ( for users that really really want a low res pano )
	{
		outBuffer = ScaleImageRGBA( data, width, height, outW, outH, IMAGE_FILTER_CUBIC );
	}
	free( data );

	return outBuffer;
}

unsigned char * PanoBrowser::LoadThumbnail( const char * filename, int & width, int & height )
{
	return TurboJpegLoadFromFile( filename, &width, &height );
}

String PanoBrowser::ThumbName( const String & s )
{
	String	ts( s );
	ts.StripTrailing( ".x" );
	ts = OVR::StringUtils::SetFileExtensionString( ts, ".thm" );
	//ts += ".jpg";
	return ts;
}

void PanoBrowser::OnMediaNotFound( App * app, String & title, String & imageFile, String & message )
{
	VrLocale::GetString( app->GetOvrMobile(), "@string/app_name", "@string/app_name", title );
	imageFile = "assets/sdcard.png";
	VrLocale::GetString( app->GetOvrMobile(), "@string/media_not_found", "@string/media_not_found", message );
	BitmapFont & font = app->GetDefaultFont();
	OVR::Array< OVR::String > wholeStrs;
	wholeStrs.PushBack( "Gear VR" );
	font.WordWrapText( message, 1.4f, wholeStrs );
}

bool PanoBrowser::OnTouchUpNoFocused()
{
	Oculus360Photos * photos = ( Oculus360Photos * )AppPtr->GetAppInterface();
	OVR_ASSERT( photos );
	if ( photos->GetActivePano() != NULL && IsOpen() && !GazingAtMenu() )
	{
		AppPtr->GetGuiSys().CloseMenu( AppPtr, this, false );
		return true;
	}
	return false;
}

}
