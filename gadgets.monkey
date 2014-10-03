Strict



Import main


Class CheckBox Extends Gadget
	Field state:Int
	Field name:String
	Field on:Int
	
	Method New( x:Int, y:Int, name:String, on:Int = 0 )
		Self.x = x; Self.y = y
		w = 16; h = 16
		Self.name = name
		Self.on = on
	End
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
			'TODO, different for drag and move operations
			'''Case _EVENT_MOUSE_ENTER
			'''	state = 1
			'''Case _EVENT_MOUSE_LEAVE
			'''	state = 0
			Case EVENT_MOUSE_DOWN_LEFT
				state = 2
			Case EVENT_MOUSE_UP_LEFT
				state = 0
				Local gadget:Gadget = APP.window.HandleEvent( new Event( EVENT_SECRET ) )
				
				If gadget = Self
					 on = 1 - on
					 parent.HandleGadgetEvent( New GadgetEvent( Self ) )
				EndIf
			Default
		End
		
		Return Self
	End
	
	Method OnRender:Void()
		SetColor 112, 146, 190
		
		'TODO
		#Rem
		Select state
			Case 0
				SetColor 100, 100, 100
			Case 1
				SetColor 200, 200, 200
			Case 2
				SetColor 255, 255, 0
			Default
		End
		#End
		
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		
		If on = 1
			SetColor 112, 146, 190
			DrawLine 1, 1, w - 2, h - 2
			DrawLine 1, h - 2, w - 2, 1
		EndIf
	End
End



#Rem
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
#End


Class Slider Extends Gadget
	Field name:String
	Field index:Int
	
	
	Method New( x:Int, y:Int, name:String, index:Int = 3 )
		Self.x = x; Self.y = y
		Self.w = 64 - 2; Self.h = 16
		Self.name = name; Self.index = index
	End
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
			Case EVENT_MOUSE_DOWN_LEFT
				sx = index
			Case EVENT_MOUSE_DRAG_LEFT
				index = sx + event.dx / 3.0
				index = Max( 0, index )
				index = Min( 18, index )
			Case EVENT_MOUSE_UP_LEFT
				parent.HandleGadgetEvent( New GadgetEvent( Self ) )
			Default
		End
		
		Return Self
	End
	
	Method OnRender:Void()
		SetColor 112, 146, 190
		DrawRect 0, 0, w , h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		SetColor 112, 146, 190
		DrawRect 3 * index, 1, 7, h -  2
		SetColor 255, 255, 255
		DrawText name, 2, 1
		'''Local s:Scissor = PopScissor()
		DrawText "%" + ( 5 * ( index + 1 ) ), w + 4, 2
		'''SetViewport( s.x, s.y, s.w, s.h )
		'''PushScissor()
	End
End



Class ButtonGadget Extends Gadget
	Field state:Int
	
	Method New( x:Int, y:Int )
		Self.x = x; Self.y = y
		Self.w = 16; Self.h = 16
	End
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
			Case EVENT_MOUSE_UP_LEFT
				Local gadget:Gadget = APP.window.HandleEvent( New Event( EVENT_SECRET ) )
				
				If gadget = Self
					parent.HandleGadgetEvent( New GadgetEvent( Self ) )
				EndIf
				
				state = 0
			Case EVENT_MOUSE_DOWN_LEFT
				state = 1
			Default
		End
		
		Return Self
	End
	
	Method OnRender:Void()
		SetColor 112, 146, 190
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		SetColor 112, 146, 190
		DrawRect 4, 4, w - 8, h - 8
	End
End



Class NumberBox Extends ContainerGadget
	Field downButton:ButtonGadget
	Field upButton:ButtonGadget
	Field name:String
	Field value:Int, minimum:Int, maximum:Int
	
	Method New( x:Int, y:Int, name:String, value:Int, minimum:Int, maximum:Int )
		Self.x = x; Self.y = y
		Self.w = 16 + 2 + TextWidth( "00" ) + 2 + 16; Self.h = 16
		Self.name = name
		Self.value = value
		Self.minimum = minimum
		Self.maximum = maximum
		downButton = New ButtonGadget( 0, 0 )
		upButton = New ButtonGadget( 0 + 16 + 2 + TextWidth( "00" ) + 2, 0 )
		AddChild( downButton )
		AddChild( upButton )
	End
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		If event.source = downButton
			value = Max( value - 1, minimum )
		ElseIf event.source = upButton
			value = Min( value + 1, maximum )
		EndIf
		
		parent.HandleGadgetEvent( New GadgetEvent( Self ) )
	End
	
	Method OnRenderInterior:Void()
		SetColor 255, 255, 255
		Super.OnRenderInterior()
		SetColor 255, 255, 255
		DrawText value, 0 + 16 + 2, 2
	End
End



Class DropList Extends Gadget
	Field values:String[]
	Field name:String
	Field index:Int
	Field mode:Int
	
	Method New( x:Int, y:Int, name:String, values:String[], index:Int )
		Self.x = x; Self.y = y
		w = 64; h = 16
		Self.name = name
		Self.values = values
		Self.index = index
	End
	
	Method HandleEvent:Gadget( event:Event )
		Select event.id
			Case EVENT_MOUSE_DOWN_LEFT
				If mode = 0
					mode = 1
					h = values.Length * 16 + 16
				Else
					mode = 0
					h = 16
					Local n:Int = event.y / 16
					
					If n <> 0
						index = n - 1
						parent.HandleGadgetEvent( New GadgetEvent( Self ) )
					EndIf
				EndIf
			Default
		End
		
		Return Self
	End
	
	Method OnRender:Void()
		SetColor 112, 146, 190
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
		SetColor 255, 255, 255
		DrawText values[ index ], 2, 2
		
		If mode = 1
			For Local n:Int = 0 Until values.Length
				DrawText values[n], 2, 2 + 16 * n + 16
			Next
		EndIf
	End
End



Global ruleTables := New IntMap< Rules >()



Class Rules
	Field id:Int
	Field value:Int[]
	
	Method New( id:Int, value:Int[] )
		Self.id = id
		Self.value = value
	End
	
	Method Copy:Rules()
		Return New Rules( id, value.Resize( value.Length ) )
	End
End



Class RuleTable Extends ContainerGadget
	Field name:String
	Field rules:Rules
	
	Field checkBoxes:CheckBox[]
	
	Method New( x:Int, y:Int, name:String, rulesId:Int )
		Self.x = x; Self.y = y
		w = 32 + 16
		Local rules:Rules = ruleTables.Get( rulesId )
		
		If rules = Null
			rules = New Rules( rulesId, [ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ] )
			ruleTables.Insert( rulesId, rules )
		EndIf
		
		Self.rules = rules
		h = rules.value.Length * 16
		Self.name = name
		checkBoxes = checkBoxes.Resize( rules.value.Length )
		
		For Local n:Int = 0 until checkBoxes.Length()
			Local checkBox:CheckBox = New CheckBox( 32, n * 16, "", rules.value[n] )
			checkBoxes[n] = checkBox
			AddChild( checkBox )
		Next
	End
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		For Local n:Int = 0 Until checkBoxes.Length
			If event.source = checkBoxes[n]
				rules.value[n] = checkBoxes[n].on
			EndIf
		Next
		
		parent.HandleGadgetEvent( New GadgetEvent( Self ) )
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		
		For Local n:Int = 0 Until rules.value.Length
			Local b:Int = n Mod 9
			Local a:Int = ( n - b ) / 9
			DrawText a, 4, n * 16
			DrawText b, 4 + 16, n * 16
			DrawText rules.value[ n ], 4 + 32, n * 16
		Next
		
		Super.OnRender()
	End
End