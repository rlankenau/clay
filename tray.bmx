Type TTray Extends Gadget
	Field boxes:TList = New TList
	Field xMax:Int
	
	Field boxOver:Box
	Field boxSelected:Box
	
	Function Make:TTray( x:Int, y:Int, w:Int, h:Int )
		Local tray:TTray = New TTray
		tray.x = x
		tray.y = y
		tray.w = w
		tray.h = h
		
		x = 10; y = 5
		
		For Local template:Template = EachIn templates
			Local box:Box = Box.Make( x, y, template )
			tray.boxes.AddLast( box )
			x = x + box.w + 10
		Next
				
		tray.xMax = Max( tray.w, x ) - tray.w
		
		Return tray
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case _EVENT_MOUSE_MOVE
			UpdateHover( event )
		Case _EVENT_MOUSE_DRAG
			Select _dragMode
			Case DRAG_BOX
				boxSelected.x = sx + _mx - _px
				boxSelected.y = sy + _my - _py
			Case DRAG_WIRE
				'TODO
			Case DRAG_PATCH
				ox = sx + event.dx
				ox = -Max( 0, Min( -ox, xMax ) )
			EndSelect
		Case _EVENT_MOUSE_DOWN
			If event.data = MOUSE_RIGHT
				_dragMode = DRAG_PATCH
				sx = ox; sy = oy
			ElseIf boxOver <> Null
				_dragMode = DRAG_BOX
				boxSelected = boxOver
				boxes.Remove( boxSelected )
				boxes.AddLast( boxSelected )
				sx = boxSelected.x; sy = boxSelected.y
			Else
				boxSelected = Null
			EndIf
		Case _EVENT_MOUSE_UP
			If _dragMode = DRAG_BOX
				boxSelected.x = sx; boxSelected.y = sy
				Local gadget:Gadget = root.HandleEvent( MakeSecretEvent() )
				
				If gadget = patch
					Local box:Box = Box.Make( patch.LocalX( sx + _mx - _px + ox ), patch.LocalY( sy + _my - _py + oy ), _GetTemplate( boxSelected.kind ) )
					
					If boxSelected.kind = "view"
						box = MakeViewBox( patch.LocalX( sx +_mx - _px + ox ), patch.LocalY( sy + _my - _py + oy ) )
					EndIf
					
					patch.boxes.AddLast( box )
				EndIf
				
				boxSelected = Null
			EndIf
		Case _EVENT_MOUSE_DOUBLE_CLICK
			If boxOver <> Null And boxOver.IsClickable() And boxOver.outs = 0
				boxOver.Execute()
			EndIf
			
			event.id = _EVENT_MOUSE_DOWN
			HandleEvent( event )
		EndSelect
		
		Return Self
	EndMethod
	
	Method UpdateHover( event:Event )
		boxOver = Null
		
		For Local box:Box = EachIn boxes
			If Contains( box.x, box.y, box.w, box.h, event.x, event.y )
				boxOver = box
			EndIf
		Next
	EndMethod
	
	Method RenderExterior()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	EndMethod
	
	Method RenderInterior()
		For Local box:Box = EachIn boxes
			box.Render()
		Next
		
		If boxSelected <> Null
			DisableScissor()
			boxSelected.Render()
			EnableScissor()
		EndIf
	EndMethod
EndType



Type TView Extends Gadget
	Field box:Box
	
	Function Make:TView( x:Int, y:Int, w:Int, h:Int )
		Local view:TView = New TView
		view.x = x
		view.y = y
		view.w = w
		view.h = h
		view.box = MakeViewBox( 8, 8 )
		Return view
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
	EndMethod
	
	Method RenderExterior()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	EndMethod
	
	Method RenderInterior()
		box.Render()
	EndMethod
EndType