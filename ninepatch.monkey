Strict



Import main



Class NinePatchImage
	Field patches:Image[9]
	Field w0:Int, w2:Int, h0:Int, h2:Int
	
	'TODO rethink all this
	Field xSafe:Int, ySafe:Int, wSafe:Int, hSafe:Int
	Field xOffset:Int, yOffset:Int, wOffset:Int, hOffset:Int
	
	Method safeWidth:Int( width:Int ); Return width - w0 - w2 + wSafe; End
	Method safeHeight:Int( height:Int ); Return height - h0 - h2 + hSafe; End
	
	'TODO rename these
	Method GetWidthForGivenSafeWidth:Int( safeWidth:Int ); Return safeWidth + w0 + w2 - wSafe; End
	Method GetHeightForGivenSafeHeight:Int( safeHeight:Int ); Return safeHeight + h0 + h2 - hSafe; End
	
	Method minimumWidth:Int() Property; Return w0 + 1 + w2; End
	Method minimumHeight:Int() Property; Return h0 + 1 + h2; End
	
	Method New( source:Image, parameters:Int[], safetyParameters:Int[] = [ -1 ] )
		Local w0:Int = parameters[0]
		Local w1:Int = parameters[1]
		Local w2:Int = source.Width() - w0 - w1
		
		Local h0:Int = parameters[2]
		Local h1:Int = parameters[3]
		Local h2:Int = source.Height() - h0 - h1
		
		Local x0:Int = 0
		Local x1:Int = w0
		Local x2:Int = w0 + w1
		
		Local y0:Int = 0
		Local y1:Int = h0
		Local y2:Int = h0 + h1
		
		patches[0] = source.GrabImage( x0, y0, w0, h0 )
		patches[1] = source.GrabImage( x1, y0,  1, h0 )
		patches[2] = source.GrabImage( x2, y0, w2, h0 )
		
		patches[3] = source.GrabImage( x0, y1, w0,  1 )
		patches[4] = source.GrabImage( x1, y1,  1,  1 )
		patches[5] = source.GrabImage( x2, y1, w2,  1 )
	
		patches[6] = source.GrabImage( x0, y2, w0, h2 )
		patches[7] = source.GrabImage( x1, y2,  1, h2 )
		patches[8] = source.GrabImage( x2, y2, w2, h2 )
		
		Self.w0 = w0; Self.w2 = w2
		Self.h0 = h0; Self.h2 = h2
		
		If safetyParameters.Length = 4
			xSafe = safetyParameters[0]
			wSafe = safetyParameters[1] - w1
			ySafe = safetyParameters[2]
			hSafe = safetyParameters[3] - h1
		EndIf
	End
	
	#Rem
	Method Draw:Void( position:IPosition< Int >, size:ISize< Int > )
		Draw( position.x, position.y, size.width, size.height )
	End
	#End
	
	Method Draw:Void( x:Int, y:Int, width:Int, height:Int )
		x -= xOffset; y -= yOffset
		width += xOffset + wOffset; height += yOffset + hOffset
		
		width = Max( width, minimumWidth )
		height = Max( height, minimumHeight )
		
		Local w1:Int = width - w0 - w2
		Local h1:Int = height - h0 - h2
		
		Local x0:Int = x
		Local x1:Int = x0 + w0
		Local x2:Int = x1 + w1
		
		Local y0:Int = y
		Local y1:Int = y0 + h0
		Local y2:Int = y1 + h1
		
		DrawImage( patches[0], x0, y0 )
		DrawImage( patches[1], x1, y0, 0, w1, 1 )
		DrawImage( patches[2], x2, y0 )
		
		DrawImage( patches[3], x0, y1, 0,  1, h1 )
		DrawImage( patches[4], x1, y1, 0, w1, h1 )
		DrawImage( patches[5], x2, y1, 0,  1, h1 )
		
		DrawImage( patches[6], x0, y2 )
		DrawImage( patches[7], x1, y2, 0, w1, 1 )
		DrawImage( patches[8], x2, y2 )
		
		'SetColor 255, 0, 0
		'DrawRect x + xSafe, y + ySafe, safeWidth( width ), safeHeight( height )
	End
End