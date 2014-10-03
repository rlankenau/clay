Type TPanel Extends GadgetContainer
	Field yMax:Int = 50
	
	Function Make:TPanel( x:Int, y:Int, w:Int, h:Int )
		Local panel:TPanel = New TPanel
		panel.x = x
		panel.y = y
		panel.w = w
		panel.h = h
		Return panel
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
		Local gadget:Gadget = Super.HandleEvent( event )
		
		If gadget <> Self
			Return gadget
		EndIf
		
		Select event.id
		Case _EVENT_MOUSE_DRAG
			oy = sy + event.dy
			oy = -Max( 0, Min( -oy, yMax ) )
		Case _EVENT_MOUSE_DOWN
			If event.data = MOUSE_RIGHT
				_dragMode = DRAG_PATCH
				sy = oy
			EndIf
		EndSelect
		
		Return Self
	EndMethod
	
	Method HandleGadgetEvent( event:GadgetEvent )
		Select True
		Case FloatProperty( event.source ) <> Null
			Local f:FloatProperty = FloatProperty( event.source )
			Setting( MapValueForKey( boxSelected.settings, f.name ) ).value = String( f._value )
			boxSelected.Execute()
		Case NumberBox( event.source ) <> Null
			Local n:NumberBox = NumberBox( event.source )
			Setting( MapValueForKey( boxSelected.settings, n.name ) ).value = String( n.value )
			boxSelected.Execute()
		EndSelect
	EndMethod
	
	Method RenderExterior()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	EndMethod
	
	Method RenderInterior()
		Super.RenderInterior()
	EndMethod
EndType