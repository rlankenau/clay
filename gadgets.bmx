Type CheckBox Extends Gadget
	Field state:Int
	Field on:Byte
	
	Function Make:CheckBox( x:Int, y:Int, on:Byte = False )
		Local check:CheckBox = New CheckBox
		check.x = x
		check.y = y
		check.w = 16
		check.h = 16
		check.on = on
		Return check
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		'TODO, different for drag and move operations
		Case _EVENT_MOUSE_ENTER
			state = 1
		Case _EVENT_MOUSE_LEAVE
			state = 0
		Case _EVENT_MOUSE_DOWN
			state = 2
		Case _EVENT_MOUSE_UP
			state = 0
			Local gadget:Gadget = root.HandleEvent( MakeSecretEvent() )
			
			If gadget = Self
				on = Not on
			EndIf
		EndSelect
		
		Return Self
	EndMethod
	
	Method RenderExterior()
		SetColor 112, 146, 190
		
		'TODO
		Rem
		Select state
		Case 0
			SetColor 100, 100, 100
		Case 1
			SetColor 200, 200, 200
		Case 2
			SetColor 255, 255, 0
		EndSelect
		EndRem
		
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		
		If on
			SetColor 112, 146, 190
			DrawLine 1, 1, w - 2, h - 2
			DrawLine 1, h - 2, w - 2, 1
		EndIf
	EndMethod
	
	Method RenderInterior()
	
	EndMethod
EndType



Type BoolProperty Extends GadgetContainer
	Field name:String
	Field _value:Byte
	
	Function Make:BoolProperty( x:Int, y:Int, name:String, value:Byte )
		Local bool:BoolProperty = New BoolProperty
		bool.x = x
		bool.y = y
		bool.w = 16
		bool.h = 16
		bool.name = name
		bool._value = value
		bool.AddChild( CheckBox.Make( 0, 0, value ) )
		Return bool
	EndFunction
	
	Method RenderInterior()
		Super.RenderInterior()
		Local s:Scissor = PopScissor()
		SetColor 255, 255, 255
		DrawText name, 20, 0
		SetViewport( s.x, s.y, s.w, s.h )
		PushScissor()
	EndMethod
EndType



Type Slider Extends Gadget
	Field name:String
	Field index:Int

	Function Make:Slider( x:Int, y:Int, name:String, index:Int = 3 )
		Local slider:Slider = New Slider
		slider.x = x
		slider.y = y
		slider.w = 64 - 2
		slider.h = 16
		slider.name = name
		slider.index = index
		Return slider
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case _EVENT_MOUSE_DOWN
			sx = index
		Case _EVENT_MOUSE_DRAG
			index = sx + event.dx / 3.0
			index = Max( 0, index )
			index = Min( 18, index )
		Case _EVENT_MOUSE_UP
			parent.HandleGadgetEvent( GadgetEvent.Make( Self ) )
		EndSelect
		
		Return Self
	EndMethod
	
	Method RenderExterior()
		SetColor 112, 146, 190
		DrawRect 0, 0, w , h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		SetColor 112, 146, 190
		DrawRect 3 * index, 1, 7, h -  2
		SetColor 255, 255, 255
		DrawText name, 2, 1
		Local s:Scissor = PopScissor()
		DrawText "%" + ( 5 * ( index + 1 ) ), w + 4, 2
		SetViewport( s.x, s.y, s.w, s.h )
		PushScissor()
	EndMethod
	
	Method RenderInterior()
	
	EndMethod
EndType



Type FloatProperty Extends GadgetContainer
	Field name:String
	Field _value:Int
	
	Function Make:FloatProperty( x:Int, y:Int, name:String, value:Int )
		Local f:FloatProperty = New FloatProperty
		f.x = x
		f.y = y
		f.w = 100
		f.h = 16
		f.name = name
		f._value = value
		f.AddChild( Slider.Make( 0, 0, name, value ) )
		Return f
	EndFunction
	
	Method HandleGadgetEvent( event:GadgetEvent )
		_value = Slider( event.source ).index
		parent.HandleGadgetEvent( GadgetEvent.Make( Self ) )
	EndMethod
	
	Method RenderExterior()
		Super.RenderExterior()
	EndMethod
	
	Method RenderInterior()
		Super.RenderInterior()
	EndMethod
EndType



Type Button Extends Gadget
	Field state:Int
	
	Function Make:Button( x:Int, y:Int )
		Local b:Button = New Button
		b.x = x; b.y = y
		b.w = 16; b.h = 16
		Return b
	EndFunction
	
	Method Go()
		parent.HandleGadgetEvent( GadgetEvent.Make( Self ) )
	EndMethod
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case _EVENT_MOUSE_UP
			Local gadget:Gadget = root.HandleEvent( MakeSecretEvent() )
			
			If gadget = Self
				Go()
			EndIf
			
			state = 0
		Case _EVENT_MOUSE_DOWN
			state = 1
		EndSelect
		
		Return Self
	EndMethod
	
	Method RenderExterior()
	
	EndMethod
	
	Method RenderInterior()
		SetColor 112, 146, 190
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		SetColor 112, 146, 190
		DrawRect 4, 4, w - 8, h - 8
	EndMethod
EndType



Type NumberBox Extends GadgetContainer
	Field downButton:Button
	Field upButton:Button
	Field name:String
	Field value:Int, minimum:Int, maximum:Int
	
	Function Make:NumberBox( x:Int, y:Int, name:String, value:Int, minimum:Int, maximum:Int )
		Local n:NumberBox = New NumberBox
		n.x = x; n.y = y
		n.w = 16 + 2 + TextWidth( "00" ) + 2 + 16
		n.h = 16
		n.name = name
		n.value = value
		n.minimum = minimum
		n.maximum = maximum
		n.downButton = Button.Make( 0, 0 )
		n.upButton = Button.Make( 0 + 16 + 2 + TextWidth( "00" ) + 2, 0 )
		n.AddChild( n.downButton )
		n.AddChild( n.upButton )
		Return n
	EndFunction
	
	Method HandleGadgetEvent( event:GadgetEvent )
		If event.source = downButton
			value = Max( value - 1, minimum )
		ElseIf event.source = upButton
			value = Min( value + 1, maximum )
		EndIf
		
		parent.HandleGadgetEvent( GadgetEvent.Make( Self ) )
	EndMethod
	
	Method RenderInterior()
		SetColor 255, 255, 255
		Super.RenderInterior()
		SetColor 255, 255, 255
		DrawText value, 0 + 16 + 2, 2
	EndMethod
EndType



Rem
Type DropList Extends Gadget
	Field values:String[]
	Field index:Int
	Field mode:Int
	
	Function Make:DropList( x:Int, y:Int, values:String[], index:Int )
		Local drop:DropList = New DropList
		drop.x = x
		drop.y = y
		drop.w = 64
		drop.h = 16
		drop.values = values
		drop.index = index
		Return drop
	EndFunction
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
		Case _EVENT_MOUSE_DOWN
		EndSelect
	EndMethod
EndType
EndRem