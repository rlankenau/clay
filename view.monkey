Strict


Import main



Class View Extends Gadget
	Field box:Box
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x; Self.y = y; Self.w = w; Self.h = h
		box = New Box( 8, 8, _GetTemplate( "view" ) )
	End
	
	Method HandleEvent:Gadget( event:Event )
		If PROJ.boxSelected = Null Then Return Self
		
		Select event.id
			Case EVENT_MOUSE_DOWN_LEFT, EVENT_MOUSE_DRAG_LEFT
				Local xx:Int = event.x - 8 - 2; xx /= 4
				Local yy:Int = event.y - 8 - 2; yy /= 4
				
				If xx >= 0 And xx < 20 And yy >= 0 And yy < 15
					PROJ.boxSelected.state[ xx ][ yy ] = 1
					PROJ.panel._ExecuteBoxSelectedIfSatisfied()
				EndIf
			Case EVENT_MOUSE_DOWN_RIGHT, EVENT_MOUSE_DRAG_RIGHT
				Local xx:Int = event.x - 8 - 2; xx /= 4
				Local yy:Int = event.y - 8 - 2; yy /= 4
				
				If xx >= 0 And xx < 20 And yy >= 0 And yy < 15
					PROJ.boxSelected.state[ xx ][ yy ] = 0
					PROJ.panel._ExecuteBoxSelectedIfSatisfied()
				EndIf
			Default
		End
		
		Return Self
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		
		SetColor 255, 255, 255
		box.Render()
	End
End