Strict



Import main



Class Patch Extends ViewGadget
	Field boxOver:Box, inOver:Int, outOver:Int
	
	Field boxes := New List< Box >()
	Field wires := New List< Wire >()
	Field sparks := New List< Spark >()
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x; Self.y = y; Self.w = w; Self.h = h
	End
	
	Method _GetBoxById:Box( id:Int )
		For Local box:Box = EachIn boxes
			If box.id = id Then Return box
		Next
		
		Return Null
	End
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case EVENT_MOUSE_MOVE
			UpdateHover( event, False )
		Case EVENT_MOUSE_DRAG_LEFT
			Local generous:Int = False
			
			If _dragMode = DRAG_WIRE
				generous = True
			EndIf
			
			UpdateHover( event, generous )
			
			Select _dragMode
			Case DRAG_BOX
				PROJ.boxSelected.x = sx + event.dx
				PROJ.boxSelected.y = sy + event.dy
			Case DRAG_WIRE
				'TODO
			End
		Case EVENT_MOUSE_DRAG_RIGHT
			'ox = sx + event.dx
			'oy = sy + event.dy
		Case EVENT_MOUSE_DOWN_LEFT
			_dragMode = DRAG_NONE
			
			If boxOver <> Null
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
					boxes.Remove( PROJ.boxSelected )
					boxes.AddLast( PROJ.boxSelected )
					sx = PROJ.boxSelected.x; sy = PROJ.boxSelected.y
				EndIf
			Else
				SelectBox( Null )
			EndIf
		Case EVENT_MOUSE_DOWN_RIGHT
			'sx = ox; sy = oy
		Case EVENT_MOUSE_UP_LEFT
			If _dragMode = DRAG_WIRE
				If boxOver <> Null
					If inOver <> -1
						If Not CycleCheck( boxOver, from )
							wires.AddLast( New Wire( from, boxOver, inOver ) )
						EndIf
					EndIf
				EndIf
			ElseIf _dragMode = DRAG_BOX
				Local gadget:Gadget = window.HandleEvent( New Event( EVENT_SECRET ) )
			
				If gadget <> PROJ.patch
					DeleteBox( PROJ.boxSelected )
					SelectBox( Null )
				EndIf
			EndIf
			_dragMode = DRAG_NONE
		Case EVENT_MOUSE_UP_RIGHT
			
		Case EVENT_MOUSE_DOUBLE_CLICK_LEFT
			If PROJ.boxSelected <> Null And PROJ.boxSelected = boxOver And PROJ.boxSelected.ins = 0
				PROJ.boxSelected.Execute()
			EndIf
		End
		
		Return Self
	End
	
	Method UpdateHover:Void( event:Event, generous:Int )
		boxOver = Null
		inOver = -1
		outOver = False
		
		For Local box:Box = EachIn boxes
			If RectangleContainsPoint( box.x, box.y, box.w, box.h, event.x, event.y )
				boxOver = box
			EndIf
		Next
		
		If boxOver <> Null
			For Local n:Int = 0 Until boxOver.ins
				If generous
					If RectangleContainsPoint( boxOver.x + ( n - 0.5 ) * boxOver.gap, boxOver.y, boxOver.gap, boxOver.h, event.x, event.y ) Or boxOver.ins = 1
						inOver = n
					EndIf
				Else
					If RectangleContainsPoint( boxOver.x + n * boxOver.gap, boxOver.y, 8, 3, event.x, event.y )
						inOver = n
					EndIf
				EndIf
			Next
			
			For Local n:Int = 0 Until boxOver.outs
				If RectangleContainsPoint( boxOver.x + n * boxOver.gap, boxOver.y + boxOver.h - 3, 8, 3, event.x, event.y )
					outOver = True
				EndIf
			Next
		EndIf
	End
	
	
	Method CycleCheck:Bool( a:Box, b:Box )
		Local list := New List< Box >()
		list.AddLast( b )
		Local count:Int = 0
	
		While list.Count() <> count
			count = list.Count()
		
			For Local wire:Wire = EachIn wires
				If list.Contains( wire.b )
					If Not list.Contains( wire.a )
						list.AddLast( wire.a )
					EndIf
				EndIf
			Next
		Wend
	
		If list.Contains( a )
			Return True
		EndIf
	
		Return False
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
		
		For Local wire:Wire = EachIn wires
			wire.Render()
		Next
		
		For Local spark:Spark = EachIn sparks
			spark.Render()
		Next
		
		SetColor 255, 255, 255
		
		'''ShowMouse()
			
		Select _dragMode
			Case DRAG_NONE
				If outOver
					'''HideMouse()
						'TODO replace _Local with GetLocal( x, Self, null)
					DrawImage imgO, GetLocalX( window._mouseX ), GetLocalY( window._mouseY )
				ElseIf inOver <> -1
					Local yes:Int = False
					
					For Local wire:Wire = EachIn wires
						If wire.b = boxOver And wire.bId = inOver
							yes = True
							Exit
						EndIf
					Next
					
					If yes
						'''HideMouse()
						DrawImage imgX, GetLocalX( window._mouseX ), GetLocalY( window._mouseY )
					EndIf
				EndIf
			Case DRAG_WIRE
				If boxOver = Null
					Wire.DrawFrom( from, GetLocalX( window._mouseX ), GetLocalY( window._mouseY ) )
				Else
					Wire.DrawFromTo( from, boxOver, inOver )
				EndIf
			Default
		End
	End
End



Function SelectBox:Void( box:Box )
	PROJ.boxSelected = box
	PROJ.panel.children.Clear()
	If box = Null Then Return
	Local y:Int = 4
	
	For Local setting:Setting = EachIn box.settings.Values()
		Select setting.kind
			Case "f"
				PROJ.panel.AddChild( New Slider( 4, y, setting.name, setting.value ) )
			Case "i1-9"
				PROJ.panel.AddChild( New NumberBox( 4, y, setting.name, setting.value, 1, 9 ) )
			Case "dedge"
				PROJ.panel.AddChild( New DropList( 4, y, setting.name, [ "dead", "alive", "wrap" ], setting.value ) )
			Case "b"
				PROJ.panel.AddChild( New CheckBox( 4, y, setting.name, setting.value ) )
			Case "a9s8"
				PROJ.panel.AddChild( New RuleTable( 4, y, setting.name, setting.value ) )
			Default
		End
		
		'TODO trash this
		y = y + 16 + 4
	Next
End