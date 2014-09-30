Type TPatch Extends Gadget
	Field boxOver:Box, inOver:Int, outOver:Int
	
	Field boxes:TList = New TList
	Field wires:TList = New TList
	Field sparks:TList = New TList
	
	Function Make:TPatch( x:Int, y:Int, w:Int, h:Int )
		Local patch:TPatch = New TPatch
		patch.x = x; patch.y = y; patch.w = w; patch.h = h
		Return patch
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case _EVENT_MOUSE_MOVE
			UpdateHover( event, False )
		Case _EVENT_MOUSE_DRAG
			Local generous:Int = False
			
			If _dragMode = DRAG_WIRE
				generous = True
			EndIf
			
			UpdateHover( event, generous )
			
			Select _dragMode
			Case DRAG_BOX
				boxSelected.x = sx + _mx - _px
				boxSelected.y = sy + _my - _py
			Case DRAG_WIRE
				'TODO
			Case DRAG_PATCH
				ox = sx + event.dx
				oy = sy + event.dy
			EndSelect
		Case _EVENT_MOUSE_DOWN
			If event.data = MOUSE_RIGHT
				_dragMode = DRAG_PATCH
				sx = ox; sy = oy
			ElseIf boxOver <> Null
				If outOver = True
					_dragMode = DRAG_WIRE
					from = boxOver
					'outSelected = outOver 'outOver is a bool at the moment
				ElseIf inOver <> -1
					For Local wire:Wire = EachIn wires
						If wire.b = boxOver And wire.bId = inOver
							DeleteWire( wire )
						EndIf
					Next
				Else
					_dragMode = DRAG_BOX
					
					SelectBox( boxOver )
					boxes.Remove( boxSelected )
					boxes.AddLast( boxSelected )
					sx = boxSelected.x; sy = boxSelected.y
				EndIf
			Else
				SelectBox( Null )
			EndIf
		Case _EVENT_MOUSE_UP
			If _dragMode = DRAG_WIRE
				If boxOver <> Null
					If inOver <> -1
						If Not CycleCheck( boxOver, from )
							wires.AddLast( Wire.Make( from, boxOver, inOver ) )
						EndIf
					EndIf
				EndIf
			ElseIf _dragMode = DRAG_BOX
				Local gadget:Gadget = root.HandleEvent( MakeSecretEvent() )
				
				If gadget <> patch
					DeleteBox( boxSelected )
					SelectBox( Null )
				EndIf
			EndIf
		Case _EVENT_MOUSE_DOUBLE_CLICK
			If boxSelected <> Null And boxSelected = boxOver And boxSelected.ins = 0
				boxSelected.Execute()
			EndIf
			
			event.id = _EVENT_MOUSE_DOWN
			HandleEvent( event )
		EndSelect
		
		Return Self
	EndMethod
	
	Method UpdateHover( event:Event, generous:Int )
		boxOver = Null
		inOver = -1
		outOver = False
		
		For Local box:Box = EachIn boxes
			If Contains( box.x, box.y, box.w, box.h, event.x, event.y )
				boxOver = box
			EndIf
		Next
		
		If boxOver <> Null
			For Local n:Int = 0 Until boxOver.ins
				If generous
					If Contains( boxOver.x + ( n - 0.5 ) * boxOver.gap, boxOver.y, boxOver.gap, boxOver.h, event.x, event.y ) Or boxOver.ins = 1
						inOver = n
					EndIf
				Else
					If Contains( boxOver.x + n * boxOver.gap, boxOver.y, 8, 3, event.x, event.y )
						inOver = n
					EndIf
				EndIf
			Next
			
			For Local n:Int = 0 Until boxOver.outs
				If Contains( boxOver.x + n * boxOver.gap, boxOver.y + boxOver.h - 3, 8, 3, event.x, event.y )
					outOver = True
				EndIf
			Next
		EndIf
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
		
		For Local wire:Wire = EachIn wires
			wire.Render()
		Next
		
		For Local spark:Spark = EachIn sparks
			spark.Render()
		Next
		
		SetColor 255, 255, 255
		
		ShowMouse()
			
		Select _dragMode
		Case DRAG_NONE
			If outOver
				HideMouse()
				DrawImage imgO, LocalX( _mx ), LocalY( _my )
			ElseIf inOver <> -1
				Local yes:Int = False
				
				For Local wire:Wire = EachIn wires
					If wire.b = boxOver And wire.bId = inOver
						yes = True
						Exit
					EndIf
				Next
				
				If yes
					HideMouse()
					DrawImage imgX, LocalX( _mx ), LocalY( _my )
				EndIf
			EndIf
		Case DRAG_WIRE
			If boxOver = Null
				Wire.DrawFrom( from, LocalX( _mx ), LocalY( _my ) )
			Else
				Wire.DrawFromTo( from, boxOver, inOver )
			EndIf
		Default
		EndSelect
	EndMethod
EndType



Global boxSelected:Box



Function SelectBox( box:Box )
	boxSelected = box
	panel.children.Clear()
	
	If box = Null
		Return
	EndIf
	
	Local y:Int = 4
	
	For Local setting:Setting = EachIn MapValues( box.settings )
		Select setting.kind
		Case "f"
			panel.AddChild( FloatProperty.Make( 4, y, setting.name, Int( setting.value ) ) )
		Case "i1-9"
			panel.AddChild( NumberBox.Make( 4, y, setting.name, Int( setting.value ), 1, 9 ) )
		EndSelect
		
		y = y + 16 + 4
	Next
EndFunction