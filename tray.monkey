Strict



Import main



Class Tray Extends ViewGadget
	Field boxes := New List< Box >()
	Field xMax:Int
	
	Field boxOver:Box
	Field boxSelected:Box
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x; Self.y = y; Self.w = w; Self.h = h
		x = 10; y = 5
		
		For Local template:Template = EachIn templates
			Local box:Box = New Box( x, y, template )
			boxes.AddLast( box )
			x = x + box.w + 10
		Next
				
		xMax = Max( w, x ) - w
	End
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case EVENT_MOUSE_MOVE
			UpdateHover( event )
		Case EVENT_MOUSE_DRAG_LEFT
			Select _dragMode
				Case DRAG_BOX
					boxSelected.x = sx + event.dx
					boxSelected.y = sy + event.dy
				Case DRAG_WIRE
					'TODO
				Default
			End
		Case EVENT_MOUSE_DRAG_RIGHT
			ox = sx + event.dx
			ox = -Max( 0, Min( -ox, xMax ) )
		Case EVENT_MOUSE_DOWN_LEFT
			If boxOver <> Null
				_dragMode = DRAG_BOX
				boxSelected = boxOver
				boxes.Remove( boxSelected )
				boxes.AddLast( boxSelected )
				sx = boxSelected.x; sy = boxSelected.y
			Else
				boxSelected = Null
			EndIf
		Case EVENT_MOUSE_DOWN_RIGHT
			sx = ox; sy = oy
		Case EVENT_MOUSE_UP_LEFT
			If _dragMode = DRAG_BOX
				boxSelected.x = sx; boxSelected.y = sy
				Local gadget:Gadget = APP.root.HandleEvent( New Event( EVENT_SECRET ) )
				
				If gadget = APP.patch
					Local box:Box = New Box( APP.patch.LocalX( GlobalX( sx ) + event.dx ), APP.patch.LocalY( GlobalY( sy ) + event.dy ), _GetTemplate( boxSelected.kind ) )
					
					If boxSelected.kind = "view"
						box = New ViewBox( APP.patch.LocalX( GlobalX( sx ) + event.dx ), APP.patch.LocalY( GlobalY( sy ) + event.dy ) )
					EndIf
					
					APP.patch.boxes.AddLast( box )
				EndIf
				
				boxSelected = Null
				_dragMode = DRAG_NONE
			EndIf
		Case EVENT_MOUSE_DOUBLE_CLICK_LEFT
			If boxOver <> Null And boxOver.isClickable And boxOver.outs = 0
				boxOver.Execute()
			EndIf
		End
		
		Return Self
	End
	
	Method UpdateHover:Void( event:Event )
		boxOver = Null
		
		For Local box:Box = EachIn boxes
			If RectangleContainsPoint( box.x, box.y, box.w, box.h, event.x, event.y )
				boxOver = box
			EndIf
		Next
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	End
	
	Method OnRenderInterior:Void()
		For Local box:Box = EachIn boxes
			box.Render()
		Next
		
		If boxSelected <> Null
			DisableScissor()
			boxSelected.Render()
			EnableScissor()
		EndIf
	End
End



Class View Extends Gadget
	Field box:Box
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x; Self.y = y; Self.w = w; Self.h = h
		box = New ViewBox( 8, 8 )
	End
	
	Method HandleEvent:Gadget( event:Event )
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	End
	
	Method OnRenderInterior:Void()
		box.Render()
	End
End