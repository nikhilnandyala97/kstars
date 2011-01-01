/**************************************************************************
                          skymap.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : Sat Feb 10 2001
    copyright            : (C) 2001 by Jason Harris
    email                : jharris@30doradus.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "skymap.h"

#include <QCursor>
#include <QBitmap>
#include <QPainter>
#include <QPixmap>
#include <QTextStream>
#include <QFile>
#include <QPointF>
#include <QApplication>
#include <QGraphicsScene>

#include <kactioncollection.h>
#include <kconfig.h>
#include <kiconloader.h>
#include <kstatusbar.h>
#include <kmessagebox.h>
#include <kaction.h>
#include <kstandarddirs.h>
#include <ktoolbar.h>
#include <ktoolinvocation.h>
#include <kicon.h>

#include "Options.h"
#include "kstars.h"
#include "kstarsdata.h"
#include "ksutils.h"
#include "imageviewer.h"
#include "dialogs/detaildialog.h"
#include "dialogs/addlinkdialog.h"
#include "kspopupmenu.h"
#include "simclock.h"
#include "skyobjects/skyobject.h"
#include "skyobjects/ksplanetbase.h"
#include "skycomponents/skymapcomposite.h"
#include "widgets/infoboxwidget.h"
#include "projections/projector.h"
#include "projections/lambertprojector.h"
#include "projections/gnomonicprojector.h"
#include "projections/stereographicprojector.h"
#include "projections/orthographicprojector.h"
#include "projections/azimuthalequidistantprojector.h"
#include "projections/equirectangularprojector.h"
#include "texturemanager.h"

#include "skymapqdraw.h"
#include "skymapgldraw.h"

#ifdef HAVE_XPLANET
#include <KProcess>
#include <kfiledialog.h>
#endif

namespace {

    // FIXME: describe what this function do and give descriptive name
    double projectionK(double c) {
        switch ( Options::projection() ) {
        case SkyMap::Lambert:
            return sqrt( 2.0/( 1.0 + c ) );
        case SkyMap:: AzimuthalEquidistant: {
            double crad = acos(c);
            return crad/sin(crad);
        }
        case SkyMap:: Orthographic:
            return 1.0;
        case SkyMap:: Stereographic:
            return 2.0/(1.0 + c);
        case SkyMap:: Gnomonic:
            return 1.0/c;
        default: //should never get here
            kWarning() << i18n("Unrecognized coordinate projection: ") << Options::projection();
        }
        // Default to orthographic
        return 1.0;
    }

    // Draw bitmap for zoom cursor. Width is size of pen to draw with.
    QBitmap zoomCursorBitmap(int width) {
        QBitmap b(32, 32);
        b.fill(Qt::color0);
        int mx = 16, my = 16;
        // Begin drawing
        QPainter p;
        p.begin( &b );
          p.setPen( QPen( Qt::color1, width ) );
          p.drawEllipse( mx - 7, my - 7, 14, 14 );
          p.drawLine(    mx + 5, my + 5, mx + 11, my + 11 );
        p.end();
        return b;
    }

    // Draw bitmap for default cursor. Width is size of pen to draw with.
    QBitmap defaultCursorBitmap(int width) {
        QBitmap b(32, 32);
        b.fill(Qt::color0);
        int mx = 16, my = 16;
        // Begin drawing
        QPainter p;
        p.begin( &b );
          p.setPen( QPen( Qt::color1, width ) );
          // 1. diagonal
          p.drawLine (mx - 2, my - 2, mx - 8, mx - 8);
          p.drawLine (mx + 2, my + 2, mx + 8, mx + 8);
          // 2. diagonal
          p.drawLine (mx - 2, my + 2, mx - 8, mx + 8);
          p.drawLine (mx + 2, my - 2, mx + 8, mx - 8);
        p.end();
        return b;
    }
}


SkyMap* SkyMap::pinstance = 0;


SkyMap* SkyMap::Create()
{
    if ( pinstance ) delete pinstance;
    pinstance = new SkyMap();
    return pinstance;
}

SkyMap* SkyMap::Instance( )
{
    return pinstance;
}

SkyMap::SkyMap() : 
    QGraphicsView( KStars::Instance() ),
    computeSkymap(true), angularDistanceMode(false), scrollCount(0),
    data( KStarsData::Instance() ), pmenu(0),
    ClickedObject(0), FocusObject(0), TransientObject(0), m_proj(0)
{
    m_Scale = 1.0;

    ZoomRect = QRect();

    setDefaultMouseCursor();	// set the cross cursor

    QPalette p = palette();
    p.setColor( QPalette::Window, QColor( data->colorScheme()->colorNamed( "SkyColor" ) ) );
    setPalette( p );

    setFocusPolicy( Qt::StrongFocus );
    setMinimumSize( 380, 250 );
    setSizePolicy( QSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding ) );

    setMouseTracking (true); //Generate MouseMove events!
    midMouseButtonDown = false;
    mouseButtonDown = false;
    slewing = false;
    clockSlewing = false;

    ClickedObject = NULL;
    FocusObject = NULL;

    m_SkyMapDraw = 0;

    pmenu = new KSPopupMenu();

    setupProjector();

    //Initialize Transient label stuff
    TransientTimeout = 100; //fade label color every 0.1 sec
    HoverTimer.setSingleShot( true ); // using this timer as a single shot timer

    connect( &HoverTimer,     SIGNAL( timeout() ), this, SLOT( slotTransientLabel() ) );
    connect( &TransientTimer, SIGNAL( timeout() ), this, SLOT( slotTransientTimeout() ) );
    connect( this, SIGNAL( destinationChanged() ), this, SLOT( slewFocus() ) );

    // Time infobox
    m_timeBox = new InfoBoxWidget( Options::shadeTimeBox(),
                                   Options::positionTimeBox(),
                                   Options::stickyTimeBox(),
                                   QStringList(), this);
    m_timeBox->setVisible( Options::showTimeBox() );
    connect(data->clock(), SIGNAL( timeChanged() ),
            m_timeBox,     SLOT(   slotTimeChanged() ) );
    connect(data->clock(), SIGNAL( timeAdvanced() ),
            m_timeBox,     SLOT(   slotTimeChanged() ) );

    // Geo infobox
    m_geoBox = new InfoBoxWidget( Options::shadeGeoBox(),
                                  Options::positionGeoBox(),
                                  Options::stickyGeoBox(),
                                  QStringList(), this);
    m_geoBox->setVisible( Options::showGeoBox() );
    connect(data,     SIGNAL( geoChanged() ),
            m_geoBox, SLOT(   slotGeoChanged() ) );

    // Object infobox
    m_objBox = new InfoBoxWidget( Options::shadeFocusBox(),
                                  Options::positionFocusBox(),
                                  Options::stickyFocusBox(),
                                  QStringList(), this);
    m_objBox->setVisible( Options::showFocusBox() );
    connect(this,     SIGNAL( objectChanged( SkyObject*) ),
            m_objBox, SLOT(   slotObjectChanged( SkyObject*) ) );
    connect(this,     SIGNAL( positionChanged( SkyPoint*) ),
            m_objBox, SLOT(   slotPointChanged(SkyPoint*) ) );

    m_iboxes = new InfoBoxes(this);
    m_iboxes->setVisible( Options::showInfoBoxes() );
    m_iboxes->setMouseTracking( true ); // Required to generate mouse move events
    m_iboxes->addInfoBox(m_timeBox);
    m_iboxes->addInfoBox(m_geoBox);
    m_iboxes->addInfoBox(m_objBox);

#ifdef HAVE_OPENGL

    Q_ASSERT( TextureManager::getContext() ); // Should not fail, because TextureManager should be already created.
    
    m_SkyMapQDraw = new SkyMapQDraw( this );
    m_SkyMapGLDraw = new SkyMapGLDraw( this );
    m_SkyMapGLDraw->hide();
    m_SkyMapQDraw->hide();
    
    if( Options::useGL() )
        m_SkyMapDraw = m_SkyMapGLDraw;
    else
        m_SkyMapDraw = m_SkyMapQDraw;

#else
    m_SkyMapDraw = new SkyMapQDraw( this );
#endif
    
    m_SkyMapDraw->setParent( this->viewport() );
    m_SkyMapDraw->show();

    //The update timer will be destructed when SkyMap is..
    QTimer *update = new QTimer(this);
    update->setInterval(30);
    connect(update, SIGNAL(timeout()), this, SLOT(update()) );
    update->start();

}

void SkyMap::slotToggleGeoBox(bool flag) {
    m_geoBox->setVisible(flag);
}

void SkyMap::slotToggleFocusBox(bool flag) {
    m_objBox->setVisible(flag);
}

void SkyMap::slotToggleTimeBox(bool flag) {
    m_timeBox->setVisible(flag);
}

void SkyMap::slotToggleInfoboxes(bool flag) {
    m_iboxes->setVisible(flag);
}

SkyMap::~SkyMap() {
    /* == Save infoxes status into Options == */
    Options::setShowInfoBoxes( m_iboxes->isVisibleTo( parentWidget() ) );
    // Time box
    Options::setPositionTimeBox( m_timeBox->pos() );
    Options::setShadeTimeBox(    m_timeBox->shaded() );
    Options::setStickyTimeBox(   m_timeBox->sticky() );
    Options::setShowTimeBox(     m_timeBox->isVisibleTo(m_iboxes) );
    // Geo box
    Options::setPositionGeoBox( m_geoBox->pos() );
    Options::setShadeGeoBox(    m_geoBox->shaded() );
    Options::setStickyGeoBox(   m_geoBox->sticky() );
    Options::setShowGeoBox(     m_geoBox->isVisibleTo(m_iboxes) );
    // Obj box
    Options::setPositionFocusBox( m_objBox->pos() );
    Options::setShadeFocusBox(    m_objBox->shaded() );
    Options::setStickyFocusBox(   m_objBox->sticky() );
    Options::setShowFocusBox(     m_objBox->isVisibleTo(m_iboxes) );
    
    //store focus values in Options
    //If not tracking and using Alt/Az coords, stor the Alt/Az coordinates
    if ( Options::useAltAz() && ! Options::isTracking() ) {
        Options::setFocusRA(  focus()->az().Degrees() );
        Options::setFocusDec( focus()->alt().Degrees() );
    } else {
        Options::setFocusRA(  focus()->ra().Hours() );
        Options::setFocusDec( focus()->dec().Degrees() );
    }

#ifdef HAVE_OPENGL
    delete m_SkyMapGLDraw;
    delete m_SkyMapQDraw;
    m_SkyMapDraw = 0; // Just a formality
#else
    delete m_SkyMapDraw;
#endif

    delete pmenu;

    delete m_proj;
}

void SkyMap::setGeometry( int x, int y, int w, int h ) {
    QGraphicsView::setGeometry( x, y, w, h );
}

void SkyMap::setGeometry( const QRect &r ) {
    QGraphicsView::setGeometry( r );
}


void SkyMap::showFocusCoords() {
    if( focusObject() && Options::isTracking() )
        emit objectChanged( focusObject() );
    else
        emit positionChanged( focus() );
}

void SkyMap::slotTransientLabel() {
    //This function is only called if the HoverTimer manages to timeout.
    //(HoverTimer is restarted with every mouseMoveEvent; so if it times
    //out, that means there was no mouse movement for HOVER_INTERVAL msec.)
    //Identify the object nearest to the mouse cursor as the
    //TransientObject.  The TransientObject is automatically labeled
    //in SkyMap::paintEvent().
    //Note that when the TransientObject pointer is not NULL, the next
    //mouseMoveEvent calls fadeTransientLabel(), which will fade out the
    //TransientLabel and then set TransientObject to NULL.
    //
    //Do not show a transient label if the map is in motion, or if the mouse
    //pointer is below the opaque horizon, or if the object has a permanent label
    if ( ! slewing && ! ( Options::useAltAz() && Options::showGround() &&
                          SkyPoint::refract(mousePoint()->alt()).Degrees() < 0.0 ) ) {
        double maxrad = 1000.0/Options::zoomFactor();
        SkyObject *so = data->skyComposite()->objectNearest( mousePoint(), maxrad );

        if ( so && ! isObjectLabeled( so ) ) {
            setTransientObject( so );

            TransientColor = data->colorScheme()->colorNamed( "UserLabelColor" );
            if ( TransientTimer.isActive() ) TransientTimer.stop();
            update();
        }
    }
}


//Slots

void SkyMap::slotTransientTimeout() {
    //Don't fade label if the transientObject is now the focusObject!
    if ( transientObject() == focusObject() && Options::useAutoLabel() ) {
        setTransientObject( NULL );
        TransientTimer.stop();
        return;
    }

    //to fade the labels, we will need to smoothly transition the alpha
    //channel from opaque (255) to transparent (0) by step of stepAlpha
    static const int stepAlpha = 12;

    //Check to see if next step produces a transparent label
    //If so, point TransientObject to NULL.
    if ( TransientColor.alpha() <= stepAlpha ) {
        setTransientObject( NULL );
        TransientTimer.stop();
    } else {
        TransientColor.setAlpha(TransientColor.alpha()-stepAlpha);
    }

    update();
}

void SkyMap::setClickedObject( SkyObject *o ) {
	  ClickedObject = o;
}

void SkyMap::setFocusObject( SkyObject *o ) {
    FocusObject = o;
    if ( FocusObject )
        Options::setFocusObject( FocusObject->name() );
    else
        Options::setFocusObject( i18n( "nothing" ) );
}

void SkyMap::slotCenter() {
    KStars* kstars = KStars::Instance();
    TrailObject* trailObj = dynamic_cast<TrailObject*>( focusObject() );
    
    setFocusPoint( clickedPoint() );
    if ( Options::useAltAz() )
        focusPoint()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );

    //clear the planet trail of old focusObject, if it was temporary
    if( trailObj && data->temporaryTrail ) {
        trailObj->clearTrail();
        data->temporaryTrail = false;
    }

    //If the requested object is below the opaque horizon, issue a warning message
    //(unless user is already pointed below the horizon)
    if ( Options::useAltAz() && Options::showGround() &&
            focus()->alt().Degrees() > -1.0 && focusPoint()->alt().Degrees() < -1.0 ) {

        QString caption = i18n( "Requested Position Below Horizon" );
        QString message = i18n( "The requested position is below the horizon.\nWould you like to go there anyway?" );
        if ( KMessageBox::warningYesNo( this, message, caption,
                                        KGuiItem(i18n("Go Anyway")), KGuiItem(i18n("Keep Position")), "dag_focus_below_horiz" )==KMessageBox::No ) {
            setClickedObject( NULL );
            setFocusObject( NULL );
            Options::setIsTracking( false );

            return;
        }
    }

    //set FocusObject before slewing.  Otherwise, KStarsData::updateTime() can reset
    //destination to previous object...
    setFocusObject( ClickedObject );
    Options::setIsTracking( true );
    if ( kstars ) {
        kstars->actionCollection()->action("track_object")->setIcon( KIcon("document-encrypt") );
        kstars->actionCollection()->action("track_object")->setText( i18n( "Stop &Tracking" ) );
    }

    //If focusObject is a SS body and doesn't already have a trail, set the temporaryTrail

    if( Options::useAutoTrail() && trailObj && trailObj->hasTrail() ) {
        trailObj->addToTrail();
        data->temporaryTrail = true;
    }

    //update the destination to the selected coordinates
    if ( Options::useAltAz() ) {
        setDestinationAltAz( focusPoint()->altRefracted(), focusPoint()->az() );
    } else {
        setDestination( focusPoint() );
    }

    focusPoint()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );

    //display coordinates in statusBar
    emit mousePointChanged( focusPoint() );
    showFocusCoords(); //update FocusBox
}

void SkyMap::slotDSS() {
    const QString URLprefix( "http://archive.stsci.edu/cgi-bin/dss_search?v=1" );
    const QString URLsuffix( "&e=J2000&h=15.0&w=15.0&f=gif&c=none&fov=NONE" );
    dms ra(0.0), dec(0.0);

    //ra and dec must be the coordinates at J2000.  If we clicked on an object, just use the object's ra0, dec0 coords
    //if we clicked on empty sky, we need to precess to J2000.
    if ( clickedObject() ) {
        ra  = clickedObject()->ra0();
        dec = clickedObject()->dec0();
    } else {
        //move present coords temporarily to ra0,dec0 (needed for precessToAnyEpoch)
        clickedPoint()->setRA0( clickedPoint()->ra().Hours() );
        clickedPoint()->setDec0( clickedPoint()->dec().Degrees() );
        clickedPoint()->precessFromAnyEpoch( data->ut().djd(), J2000 );
        ra  = clickedPoint()->ra();
        dec = clickedPoint()->dec();

        //restore coords from present epoch
        clickedPoint()->setRA(  clickedPoint()->ra0() );
        clickedPoint()->setDec( clickedPoint()->dec0() );
    }

    char decsgn = ( dec.Degrees() < 0.0 ) ? '-' : '+';
    int dd = abs( dec.degree() );
    int dm = abs( dec.arcmin() );
    int ds = abs( dec.arcsec() );
    QString RAString, DecString;
    DecString = DecString.sprintf( "&d=%c%02d+%02d+%02d", decsgn, dd, dm, ds );
    RAString  = RAString.sprintf( "&r=%02d+%02d+%02d", ra.hour(), ra.minute(), ra.second() );

    //concat all the segments into the kview command line:
    KUrl url (URLprefix + RAString + DecString + URLsuffix);

    KStars* kstars = KStars::Instance();
    if( kstars ) {
        ImageViewer *iv = new ImageViewer( url,
            i18n( "Digitized Sky Survey image provided by the Space Telescope Science Institute [public domain]." ),
            this );
        iv->show();
    }
}

void SkyMap::slotSDSS() {
	QString URLprefix( "http://casjobs.sdss.org/ImgCutoutDR6/getjpeg.aspx?" );
	QString URLsuffix( "&scale=1.0&width=600&height=600&opt=GST&query=SR(10,20)" );
	dms ra(0.0), dec(0.0);
	QString RAString, DecString;

	//ra and dec must be the coordinates at J2000.  If we clicked on an object, just use the object's ra0, dec0 coords
	//if we clicked on empty sky, we need to precess to J2000.
	if ( clickedObject() ) {
		ra  = clickedObject()->ra0();
		dec = clickedObject()->dec0();
	} else {
		//move present coords temporarily to ra0,dec0 (needed for precessToAnyEpoch)
		clickedPoint()->setRA0( clickedPoint()->ra() );
		clickedPoint()->setDec0( clickedPoint()->dec() );
		clickedPoint()->precessFromAnyEpoch( data->ut().djd(), J2000 );
		ra  = clickedPoint()->ra();
		dec = clickedPoint()->dec();

		//restore coords from present epoch
		clickedPoint()->setRA( clickedPoint()->ra0().Hours() );
		clickedPoint()->setDec( clickedPoint()->dec0().Degrees() );
	}

	RAString = RAString.sprintf( "ra=%f", ra.Degrees() );
	DecString = DecString.sprintf( "&dec=%f", dec.Degrees() );

	//concat all the segments into the kview command line:
	KUrl url (URLprefix + RAString + DecString + URLsuffix);

    KStars* kstars = KStars::Instance();
    if( kstars ) {
        ImageViewer *iv = new ImageViewer( url,
            i18n( "Sloan Digital Sky Survey image provided by the Astrophysical Research Consortium [free for non-commercial use]." ),
            this );
        iv->show();
    }
}

void SkyMap::slotBeginAngularDistance() {
    angularDistanceMode = true;
    AngularRuler.clear();

    //If the cursor is near a SkyObject, reset the AngularRuler's 
    //start point to the position of the SkyObject
    double maxrad = 1000.0/Options::zoomFactor();
    SkyObject *so = data->skyComposite()->objectNearest( clickedPoint(), maxrad );
    if ( so ) {
        AngularRuler.append( so );
        AngularRuler.append( so );
    } else {
        AngularRuler.append( clickedPoint() );
        AngularRuler.append( clickedPoint() );
    }

    AngularRuler.update( data );
}

void SkyMap::slotEndAngularDistance() {
    if( angularDistanceMode ) {
        QString sbMessage;

        //If the cursor is near a SkyObject, reset the AngularRuler's
        //end point to the position of the SkyObject
        double maxrad = 1000.0/Options::zoomFactor();
        SkyObject *so = data->skyComposite()->objectNearest( clickedPoint(), maxrad );
        if ( so ) {
            AngularRuler.setPoint( 1, so );
            sbMessage = so->translatedLongName() + "   ";
        } else {
            AngularRuler.setPoint( 1, clickedPoint() );
        }

        angularDistanceMode=false;
        AngularRuler.update( data );
        dms angularDistance = AngularRuler.angularSize();
        AngularRuler.clear();

        sbMessage += i18n( "Angular distance: %1", angularDistance.toDMSString() );

        // Create unobsructive message box with suicidal tendencies
        // to display result.
        InfoBoxWidget* box = new InfoBoxWidget(
            true, mapFromGlobal( QCursor::pos() ), 0, QStringList(sbMessage), this);
        connect(box, SIGNAL( clicked() ), box, SLOT( deleteLater() ));
        QTimer::singleShot(5000, box, SLOT( deleteLater() ));
        box->adjust();
        box->show();
    }
}

void SkyMap::slotCancelAngularDistance(void) {
    angularDistanceMode=false;
    AngularRuler.clear();
}

void SkyMap::slotImage() {
    QString message = ((KAction*)sender())->text();
    message = message.remove( '&' ); //Get rid of accelerator markers

    // Need to do this because we are comparing translated strings
    int index = -1;
    for( int i = 0; i < clickedObject()->ImageTitle().size(); ++i ) {
        if( i18nc( "Image/info menu item (should be translated)", clickedObject()->ImageTitle().at( i ).toLocal8Bit().data() ) == message ) {
            index = i;
            break;
        }
    }

    QString sURL;
    if ( index >= 0 && index < clickedObject()->ImageList().size() ) {
        sURL = clickedObject()->ImageList()[ index ];
    } else {
        kWarning() << "ImageList index out of bounds: " << index;
        if ( index == -1 ) {
            kWarning() << "Message string \"" << message << "\" not found in ImageTitle.";
            kDebug() << clickedObject()->ImageTitle();
        }
    }

    KUrl url ( sURL );
    if( !url.isEmpty() )
        new ImageViewer( url, clickedObject()->messageFromTitle(message), this );
}

void SkyMap::slotInfo() {
    QString message = ((KAction*)sender())->text();
    message = message.remove( '&' ); //Get rid of accelerator markers

    // Need to do this because we are comparing translated strings
    int index = -1;
    for( int i = 0; i < clickedObject()->InfoTitle().size(); ++i ) {
        if( i18nc( "Image/info menu item (should be translated)", clickedObject()->InfoTitle().at( i ).toLocal8Bit().data() ) == message ) {
            index = i;
            break;
        }
    }

    QString sURL;
    if ( index >= 0 && index < clickedObject()->InfoList().size() ) {
        sURL = clickedObject()->InfoList()[ index ];
    } else {
        kWarning() << "InfoList index out of bounds: " << index;
        if ( index == -1 ) {
            kWarning() << "Message string \"" << message << "\" not found in InfoTitle.";
            kDebug() << clickedObject()->InfoTitle();
        }
    }

    KUrl url ( sURL );
    if (!url.isEmpty())
        KToolInvocation::invokeBrowser(sURL);
}

bool SkyMap::isObjectLabeled( SkyObject *object ) {
    return data->skyComposite()->labelObjects().contains( object );
}

void SkyMap::slotRemoveObjectLabel() {
    data->skyComposite()->removeNameLabel( clickedObject() );
    forceUpdate();
}

void SkyMap::slotAddObjectLabel() {
    data->skyComposite()->addNameLabel( clickedObject() );
    //Since we just added a permanent label, we don't want it to fade away!
    if ( transientObject() == clickedObject() )
        setTransientObject( NULL );
    forceUpdate();
}

void SkyMap::slotRemovePlanetTrail() {
    TrailObject* tobj = dynamic_cast<TrailObject*>( clickedObject() );
    if( tobj ) {
        tobj->clearTrail();
        forceUpdate();
    }
}

void SkyMap::slotAddPlanetTrail() {
    TrailObject* tobj = dynamic_cast<TrailObject*>( clickedObject() );
    if( tobj ) {
        tobj->addToTrail();
        forceUpdate();
    }
}

void SkyMap::slotDetail() {
    // check if object is selected
    if ( !clickedObject() ) {
        KMessageBox::sorry( this, i18n("No object selected."), i18n("Object Details") );
        return;
    }
    DetailDialog* detail = new DetailDialog( clickedObject(), data->ut(), data->geo(), KStars::Instance() );
    detail->setAttribute(Qt::WA_DeleteOnClose);
    detail->show();
}

void SkyMap::slotClockSlewing() {
    //If the current timescale exceeds slewTimeScale, set clockSlewing=true, and stop the clock.
    if( (fabs( data->clock()->scale() ) > Options::slewTimeScale())  ^  clockSlewing ) {
        data->clock()->setManualMode( !clockSlewing );
        clockSlewing = !clockSlewing;
        // don't change automatically the DST status
        KStars* kstars = KStars::Instance();
        if( kstars )
            kstars->updateTime( false );
    }
}

void SkyMap::setFocus( SkyPoint *p ) {
    setFocus( p->ra(), p->dec() );
}

void SkyMap::setFocus( const dms &ra, const dms &dec ) {
    Options::setFocusRA(  ra.Hours() );
    Options::setFocusDec( dec.Degrees() );

    focus()->set( ra, dec );
    focus()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
}

void SkyMap::setFocusAltAz( const dms &alt, const dms &az) {
    Options::setFocusRA( focus()->ra().Hours() );
    Options::setFocusDec( focus()->dec().Degrees() );
    focus()->setAlt(alt);
    focus()->setAz(az);
    focus()->HorizontalToEquatorial( data->lst(), data->geo()->lat() );

    slewing = false;
    forceUpdate(); //need a total update, or slewing with the arrow keys doesn't work.
}

void SkyMap::setDestination( SkyPoint *p ) {
    Destination = *p;
    destination()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
    emit destinationChanged();
}

void SkyMap::setDestination( const dms &ra, const dms &dec ) {
    destination()->set( ra, dec );
    destination()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
    emit destinationChanged();
}

void SkyMap::setDestinationAltAz( const dms &alt, const dms &az) {
    destination()->setAlt(alt);
    destination()->setAz(az);
    destination()->HorizontalToEquatorial( data->lst(), data->geo()->lat() );
    emit destinationChanged();
}

void SkyMap::setClickedPoint( SkyPoint *f ) { 
    ClickedPoint = *f;
}

void SkyMap::updateFocus() {
    if( slewing )
        return;

    //Tracking on an object
    if ( Options::isTracking() && focusObject() != NULL ) {
        if ( Options::useAltAz() ) {
            //Tracking any object in Alt/Az mode requires focus updates
            setFocusAltAz( focusObject()->altRefracted(), focusObject()->az() );
            focus()->HorizontalToEquatorial( data->lst(), data->geo()->lat() );
            setDestination( focus() );
        } else {
            //Tracking in equatorial coords
            setFocus( focusObject() );
            focus()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
            setDestination( focus() );
        }

    //Tracking on empty sky
    } else if ( Options::isTracking() && focusPoint() != NULL ) {
        if ( Options::useAltAz() ) {
            //Tracking on empty sky in Alt/Az mode
            setFocus( focusPoint() );
            focus()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
            setDestination( focus() );
        }

    // Not tracking and not slewing, let sky drift by
    // This means that horizontal coordinates are constant.
    } else {
        focus()->HorizontalToEquatorial(data->lst(), data->geo()->lat() );
    }
}

void SkyMap::slewFocus() {
    double dX, dY, fX, fY, r, r0;
    double step0 = 0.5;
    double step = step0;
    double maxstep = 10.0;
    
    SkyPoint newFocus;

    //Don't slew if the mouse button is pressed
    //Also, no animated slews if the Manual Clock is active
    //08/2002: added possibility for one-time skipping of slew with snapNextFocus
    if ( !mouseButtonDown ) {
        bool goSlew = ( Options::useAnimatedSlewing() &&
                        ! data->snapNextFocus() ) &&
                      !( data->clock()->isManualMode() && data->clock()->isActive() );
        if ( goSlew  ) {
            if ( Options::useAltAz() ) {
                dX = destination()->az().Degrees() - focus()->az().Degrees();
                dY = destination()->alt().Degrees() - focus()->alt().Degrees();
            } else {
                dX = destination()->ra().Degrees() - focus()->ra().Degrees();
                dY = destination()->dec().Degrees() - focus()->dec().Degrees();
            }

            //switch directions to go the short way around the celestial sphere, if necessary.
            dX = KSUtils::reduceAngle(dX, -180.0, 180.0);

            r0 = sqrt( dX*dX + dY*dY );
            r = r0;
            if ( r0 < 20.0 ) { //smaller slews have smaller maxstep
                maxstep *= (10.0 + 0.5*r0)/20.0;
            }
            while ( r > step ) {
                //DEBUG
                kDebug() << step << ": " << r << ": " << r0 << endl;
                fX = dX / r;
                fY = dY / r;

                if ( Options::useAltAz() ) {
                    focus()->setAlt( focus()->alt().Degrees() + fY*step );
                    focus()->setAz( dms( focus()->az().Degrees() + fX*step ).reduce() );
                    focus()->HorizontalToEquatorial( data->lst(), data->geo()->lat() );
                } else {
                    fX = fX/15.; //convert RA degrees to hours
                    newFocus.set( focus()->ra().Hours() + fX*step, focus()->dec().Degrees() + fY*step );
                    setFocus( &newFocus );
                    focus()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
                }

                slewing = true;
                //since we are slewing, fade out the transient label
                if ( transientObject() && ! TransientTimer.isActive() )
                    fadeTransientLabel();

                forceUpdate();
                qApp->processEvents(); //keep up with other stuff

                if ( Options::useAltAz() ) {
                    dX = destination()->az().Degrees() - focus()->az().Degrees();
                    dY = destination()->alt().Degrees() - focus()->alt().Degrees();
                } else {
                    dX = destination()->ra().Degrees() - focus()->ra().Degrees();
                    dY = destination()->dec().Degrees() - focus()->dec().Degrees();
                }

                //switch directions to go the short way around the celestial sphere, if necessary.
                dX = KSUtils::reduceAngle(dX, -180.0, 180.0);
                r = sqrt( dX*dX + dY*dY );
                
                //Modify step according to a cosine-shaped profile
                //centered on the midpoint of the slew
                //NOTE: don't allow the full range from -PI/2 to PI/2
                //because the slew will never reach the destination as 
                //the speed approaches zero at the end!
                double t = dms::PI*(r - 0.5*r0)/(1.05*r0);
                step = cos(t)*maxstep;
            }
        }

        //Either useAnimatedSlewing==false, or we have slewed, and are within one step of destination
        //set focus=destination.
        if ( Options::useAltAz() ) {
            setFocusAltAz( destination()->alt(), destination()->az() );
            focus()->HorizontalToEquatorial( data->lst(), data->geo()->lat() );
        } else {
            setFocus( destination() );
            focus()->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
        }

        slewing = false;

        //Turn off snapNextFocus, we only want it to happen once
        if ( data->snapNextFocus() ) {
            data->setSnapNextFocus(false);
        }

        //Start the HoverTimer. if the user leaves the mouse in place after a slew,
        //we want to attach a label to the nearest object.
        if ( Options::useHoverLabel() )
            HoverTimer.start( HOVER_INTERVAL );

        forceUpdate();
    }
}

void SkyMap::slotZoomIn() {
    setZoomFactor( Options::zoomFactor() * DZOOM );
}

void SkyMap::slotZoomOut() {
    setZoomFactor( Options::zoomFactor() / DZOOM );
}

void SkyMap::slotZoomDefault() {
    setZoomFactor( DEFAULTZOOM );
}

void SkyMap::setZoomFactor(double factor) {
    Options::setZoomFactor(  KSUtils::clamp(factor, MINZOOM, MAXZOOM)  );
    forceUpdate();
    emit zoomChanged();
}

const Projector * SkyMap::projector() const
{
    return m_proj;
}

// force a new calculation of the skymap (used instead of update(), which may skip the redraw)
// if now=true, SkyMap::paintEvent() is run immediately, rather than being added to the event queue
// also, determine new coordinates of mouse cursor.
void SkyMap::forceUpdate( bool now )
{
    QPoint mp( mapFromGlobal( QCursor::pos() ) );
    if (! projector()->unusablePoint( mp )) {
        //determine RA, Dec of mouse pointer
        setMousePoint( projector()->fromScreen( mp, data->lst(), data->geo()->lat() ) );
    }

    computeSkymap = true;

    // Ensure that stars are recomputed
    data->incUpdateID();

    if( now )
        m_SkyMapDraw->repaint();
    else
        m_SkyMapDraw->update();
    
}

float SkyMap::fov() {
     float diagonalPixels = sqrt(static_cast<double>( width() * width() + height() * height() ));
     return diagonalPixels / ( 2 * Options::zoomFactor() * dms::DegToRad );
}

void SkyMap::setupProjector() {
    //Update View Parameters for projection
    ViewParams p;
    p.focus         = focus();
    p.height        = height();
    p.width         = width();
    p.useAltAz      = Options::useAltAz();
    p.useRefraction = Options::useRefraction();
    p.zoomFactor    = Options::zoomFactor();
    p.fillGround    = Options::showHorizon() && Options::showGround();
    //Check if we need a new projector
    if( m_proj && Options::projection() == m_proj->type() )
        m_proj->setViewParams(p);
    else {
        delete m_proj;
        switch( Options::projection() ) {
            case Gnomonic:
                m_proj = new GnomonicProjector(p);
                break;
            case Stereographic:
                m_proj = new StereographicProjector(p);
                break;
            case Orthographic:
                m_proj = new OrthographicProjector(p);
                break;
            case AzimuthalEquidistant:
                m_proj = new AzimuthalEquidistantProjector(p);
                break;
            case Equirectangular:
                m_proj = new EquirectangularProjector(p);
                break;
            case Lambert: default:
                //TODO: implement other projection classes
                m_proj = new LambertProjector(p);
                break;
        }
    }
}

void SkyMap::setZoomMouseCursor()
{
    mouseMoveCursor = false;	// no mousemove cursor
    QBitmap cursor = zoomCursorBitmap(2);
    QBitmap mask   = zoomCursorBitmap(4);
    setCursor( QCursor(cursor, mask) );
}

void SkyMap::setDefaultMouseCursor()
{
    mouseMoveCursor = false;        // no mousemove cursor
    QBitmap cursor = defaultCursorBitmap(2);
    QBitmap mask   = defaultCursorBitmap(3);
    setCursor( QCursor(cursor, mask) );
}

void SkyMap::setMouseMoveCursor()
{
    if (mouseButtonDown)
    {
        setCursor(Qt::SizeAllCursor);	// cursor shape defined in qt
        mouseMoveCursor = true;
    }
}

void SkyMap::addLink() {
    if( !clickedObject() ) 
        return;
    QPointer<AddLinkDialog> adialog = new AddLinkDialog( this, clickedObject()->name() );
    QString entry;
    QFile file;

    if ( adialog->exec()==QDialog::Accepted ) {
        if ( adialog->isImageLink() ) {
            //Add link to object's ImageList, and descriptive text to its ImageTitle list
            clickedObject()->ImageList().append( adialog->url() );
            clickedObject()->ImageTitle().append( adialog->desc() );

            //Also, update the user's custom image links database
            //check for user's image-links database.  If it doesn't exist, create it.
            file.setFileName( KStandardDirs::locateLocal( "appdata", "image_url.dat" ) ); //determine filename in local user KDE directory tree.

            if ( !file.open( QIODevice::ReadWrite | QIODevice::Append ) ) {
                QString message = i18n( "Custom image-links file could not be opened.\nLink cannot be recorded for future sessions." );
                KMessageBox::sorry( 0, message, i18n( "Could Not Open File" ) );
                return;
            } else {
                entry = clickedObject()->name() + ':' + adialog->desc() + ':' + adialog->url();
                QTextStream stream( &file );
                stream << entry << endl;
                file.close();
                emit linkAdded();
            }
        } else {
            clickedObject()->InfoList().append( adialog->url() );
            clickedObject()->InfoTitle().append( adialog->desc() );

            //check for user's image-links database.  If it doesn't exist, create it.
            file.setFileName( KStandardDirs::locateLocal( "appdata", "info_url.dat" ) ); //determine filename in local user KDE directory tree.

            if ( !file.open( QIODevice::ReadWrite | QIODevice::Append ) ) {
                QString message = i18n( "Custom information-links file could not be opened.\nLink cannot be recorded for future sessions." );						KMessageBox::sorry( 0, message, i18n( "Could not Open File" ) );
                return;
            } else {
                entry = clickedObject()->name() + ':' + adialog->desc() + ':' + adialog->url();
                QTextStream stream( &file );
                stream << entry << endl;
                file.close();
                emit linkAdded();
            }
        }
    }
    delete adialog;
}

void SkyMap::updateAngleRuler() {
    if( angularDistanceMode && (!pmenu || !pmenu->isVisible()) )
        AngularRuler.setPoint( 1, mousePoint() );
    AngularRuler.update( data );
}

bool SkyMap::isSlewing() const  {
    return (slewing || ( clockSlewing && data->clock()->isActive() ) );
}

#ifdef HAVE_OPENGL
void SkyMap::slotToggleGL() {

    Q_ASSERT( m_SkyMapGLDraw );
    Q_ASSERT( m_SkyMapQDraw );

    m_SkyMapDraw->setParent( 0 );
    m_SkyMapDraw->hide();

    if( Options::useGL() ) {
        // Do NOT use GL
        Options::setUseGL( false );
        m_SkyMapDraw = m_SkyMapQDraw;
        KStars::Instance()->actionCollection()->action( "opengl" )->setText(i18n("Switch to OpenGL backend"));
    }
    else {
        // Use GL
        Options::setUseGL( true );

        Q_ASSERT( TextureManager::getContext() ); // Should not fail, because TextureManager should be already created.

        m_SkyMapDraw = m_SkyMapGLDraw;
        KStars::Instance()->actionCollection()->action( "opengl" )->setText(i18n("Switch to QPainter backend"));
    }
    m_SkyMapDraw->setParent( viewport() );
    m_SkyMapDraw->show();
    m_SkyMapDraw->resize( size() );
}
#endif

#ifdef HAVE_XPLANET
void SkyMap::startXplanet( const QString & outputFile ) {
    QString year, month, day, hour, minute, seconde, fov;

    // If Options::xplanetPath() is empty, return
    if ( Options::xplanetPath().isEmpty() ) {
        KMessageBox::error(0, i18n("Xplanet binary path is empty in config panel."));
        return;
    }

    // Format date
    if ( year.setNum( data->ut().date().year() ).size() == 1 ) year.push_front( '0' );
    if ( month.setNum( data->ut().date().month() ).size() == 1 ) month.push_front( '0' );
    if ( day.setNum( data->ut().date().day() ).size() == 1 ) day.push_front( '0' );
    if ( hour.setNum( data->ut().time().hour() ).size() == 1 ) hour.push_front( '0' );
    if ( minute.setNum( data->ut().time().minute() ).size() == 1 ) minute.push_front( '0' );
    if ( seconde.setNum( data->ut().time().second() ).size() == 1 ) seconde.push_front( '0' );

    // Create xplanet process
    KProcess *xplanetProc = new KProcess;

    // Add some options
    *xplanetProc << Options::xplanetPath()
            << "-body" << clickedObject()->name().toLower() 
            << "-geometry" << Options::xplanetWidth() + 'x' + Options::xplanetHeight()
            << "-date" <<  year + month + day + '.' + hour + minute + seconde
            << "-glare" << Options::xplanetGlare()
            << "-base_magnitude" << Options::xplanetMagnitude()
            << "-light_time"
            << "-window";

    // General options
    if ( ! Options::xplanetTitle().isEmpty() )
        *xplanetProc << "-window_title" << "\"" + Options::xplanetTitle() + "\"";
    if ( Options::xplanetFOV() )
        *xplanetProc << "-fov" << fov.setNum( this->fov() ).replace( '.', ',' );
    if ( Options::xplanetConfigFile() )
        *xplanetProc << "-config" << Options::xplanetConfigFilePath();
    if ( Options::xplanetStarmap() )
        *xplanetProc << "-starmap" << Options::xplanetStarmapPath();
    if ( Options::xplanetArcFile() )
        *xplanetProc << "-arc_file" << Options::xplanetArcFilePath();
    if ( Options::xplanetWait() )
        *xplanetProc << "-wait" << Options::xplanetWaitValue();
    if ( !outputFile.isEmpty() )
        *xplanetProc << "-output" << outputFile << "-quality" << Options::xplanetQuality();

    // Labels
    if ( Options::xplanetLabel() ) {
        *xplanetProc << "-fontsize" << Options::xplanetFontSize()
                << "-color" << "0x" + Options::xplanetColor().mid( 1 )
                << "-date_format" << Options::xplanetDateFormat();

        if ( Options::xplanetLabelGMT() )
            *xplanetProc << "-gmtlabel";
        else
            *xplanetProc << "-label";
        if ( !Options::xplanetLabelString().isEmpty() )
            *xplanetProc << "-label_string" << "\"" + Options::xplanetLabelString() + "\"";
        if ( Options::xplanetLabelTL() )
            *xplanetProc << "-labelpos" << "+15+15";
        else if ( Options::xplanetLabelTR() )
            *xplanetProc << "-labelpos" << "-15+15";
        else if ( Options::xplanetLabelBR() )
            *xplanetProc << "-labelpos" << "-15-15";
        else if ( Options::xplanetLabelBL() )
            *xplanetProc << "-labelpos" << "+15-15";
    }

    // Markers
    if ( Options::xplanetMarkerFile() )
        *xplanetProc << "-marker_file" << Options::xplanetMarkerFilePath();
    if ( Options::xplanetMarkerBounds() )
        *xplanetProc << "-markerbounds" << Options::xplanetMarkerBoundsPath();

    // Position
    if ( Options::xplanetRandom() )
        *xplanetProc << "-random";
    else
        *xplanetProc << "-latitude" << Options::xplanetLatitude() << "-longitude" << Options::xplanetLongitude();

    // Projection
    if ( Options::xplanetProjection() ) {
        switch ( Options::xplanetProjection() ) {
            case 1 : *xplanetProc << "-projection" << "ancient"; break;
            case 2 : *xplanetProc << "-projection" << "azimuthal"; break;
            case 3 : *xplanetProc << "-projection" << "bonne"; break;
            case 4 : *xplanetProc << "-projection" << "gnomonic"; break;
            case 5 : *xplanetProc << "-projection" << "hemisphere"; break;
            case 6 : *xplanetProc << "-projection" << "lambert"; break;
            case 7 : *xplanetProc << "-projection" << "mercator"; break;
            case 8 : *xplanetProc << "-projection" << "mollweide"; break;
            case 9 : *xplanetProc << "-projection" << "orthographic"; break;
            case 10 : *xplanetProc << "-projection" << "peters"; break;
            case 11 : *xplanetProc << "-projection" << "polyconic"; break;
            case 12 : *xplanetProc << "-projection" << "rectangular"; break;
            case 13 : *xplanetProc << "-projection" << "tsc"; break;
            default : break;
        }
        if ( Options::xplanetBackground() ) {
            if ( Options::xplanetBackgroundImage() )
                *xplanetProc << "-background" << Options::xplanetBackgroundImagePath();
            else
                *xplanetProc << "-background" << "0x" + Options::xplanetBackgroundColorValue().mid( 1 );
        }
    }

    // We add this option at the end otherwise it does not work (???)
    *xplanetProc << "-origin" << "earth";

    // Run xplanet
    kWarning() << i18n( "Run : %1" , xplanetProc->program().join(" "));
    xplanetProc->start();
}

void SkyMap::slotXplanetToScreen() {
    startXplanet();
}

void SkyMap::slotXplanetToFile() {
    QString filename = KFileDialog::getSaveFileName( );
    if ( ! filename.isEmpty() ) {
        startXplanet( filename );
    }
}
#endif

#include "skymap.moc"
