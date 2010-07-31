/*  Stream Widget
    Copyright (C) 2003 Jasem Mutlaq (mutlaqja@ikarustech.com)

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
    
    2004-03-16: A class to handle video streaming.
 */
 
 #ifndef STREAMWG_H
 #define STREAMWG_H
 
 #include <tqpixmap.h>
 #include <kpixmapio.h>
  
 #include "streamformui.h"
 #include "tqframe.h"
 
 
 class QImage;
 class QSocketNotifier;
 class VideoWG;
 class INDIStdDevice;
 class QPainter;
 class QVBoxLayout;
 
 class StreamWG : public streamForm
 {
   Q_OBJECT
   
    public:
      StreamWG(INDIStdDevice *inStdDev, TQWidget * parent =0, const char * name =0);
      ~StreamWG();
 
   friend class VideoWG;
   friend class INDIStdDevice;
   
   void setColorFrame(bool color);
   void setSize(int wd, int ht);
   void enableStream(bool enable);
   
   bool	processStream;
   int         		 streamWidth, streamHeight;
   VideoWG		*streamFrame;
   bool			 colorFrame;
      
   private:
   INDIStdDevice        *stdDev;
   TQPixmap               playPix, pausePix, capturePix;
   TQVBoxLayout           *videoFrameLayout;
   
   protected:
   void closeEvent ( TQCloseEvent * e );
   void resizeEvent(TQResizeEvent *ev);
   
   
   public slots: 
   void playPressed();
   void captureImage();


 };
 
 class VideoWG : public QFrame
 {
      Q_OBJECT
   
    public:
      VideoWG(TQWidget * parent =0, const char * name =0);
      ~VideoWG();
      
      friend class StreamWG;
      
      void newFrame(unsigned char *buffer, int buffSiz, int w, int h);
      
    private:
      int		totalBaseCount;
      QRgb              *grayTable;
      QImage		*streamImage;
      QPixmap		 qPix;
      KPixmapIO		 kPixIO;
      
    protected:
     void paintEvent(TQPaintEvent *ev);
     
};

#endif